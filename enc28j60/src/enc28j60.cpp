#include "FreeRTOS.h"
#include "task.h"

#include "enc28j60.h"
#include "enc28j60_LWIP_FreeRTOS.h"
#include "enc28j60_registers.h"
#include "errno.h"
#include "hardware/flash.h"
#include "netif/etharp.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "stdio.h"
#include "utils.h"
#include <cstring>

#ifdef ENC_DEBUG_ON
#define ENC_DEBUG_print printf
#else
#define ENC_DEBUG_print
#endif

/*
 * ERXRDPT need to be set always at odd addresses, refer to errata datasheet
 */
static uint16_t erxrdpt_workaround(uint16_t next_packet_ptr, uint16_t start, uint16_t end) {
    uint16_t erxrdpt;

    if ((next_packet_ptr - 1 < start) || (next_packet_ptr - 1 > end))
        erxrdpt = end;
    else
        erxrdpt = next_packet_ptr - 1;

    return erxrdpt;
}

namespace drivers {

    enc28j60::enc28j60(Config config) : config_{ config } {
    }

    void enc28j60::lock() {
        xSemaphoreTakeRecursive(config_.mutex, portMAX_DELAY);
    }

    void enc28j60::unlock() {
        xSemaphoreGiveRecursive(config_.mutex);
    }

    int enc28j60::rx_interrupt() {
        /* RX handler */ 
        int ret;
        int pk_counter = regb_read(EPKTCNT);
        while (pk_counter-- > 0) {
            auto packet_info = get_incoming_packet_info();

            if (packet_info.received_ok) {

                pbuf *ptr = pbuf_alloc(PBUF_RAW, packet_info.byte_count, PBUF_RAM);
                if (ptr != nullptr) {
                    get_incoming_packet(packet_info, (uint8_t *)ptr->payload, packet_info.byte_count);

                    LINK_STATS_INC(link.recv);
                    ENC_DEBUG_print("Received packet with len %d!\n", packet_info.byte_count);

                    if (netif.input(ptr, &netif) != ERR_OK) {
                        ENC_DEBUG_print("Error processing frame input\n");
                        pbuf_free(ptr);
                    }
                }
            } else {
                get_incoming_packet(packet_info, nullptr, 0); // Advance to the next packet discarding current
            }
            ret = pk_counter; 
        }
        return ret;
    }

    void enc28j60::irq_deferred_handler() {

        while (true) {
            if (xSemaphoreTake(irq_loop_sem, portMAX_DELAY) == pdPASS) {
                int loop;
                int intflags;
                /* disable further interrupts */
                reg_bfclr(EIE, EIE_INTIE);

                do {
                    loop = 0;
                    intflags = regb_read(EIR);

                    /* DMA interrupt handler (not currently used) */
                    if ((intflags & EIR_DMAIF) != 0) {
                        loop++;
                        reg_bfclr(EIR, EIR_DMAIF);
                    }

                    /* LINK changed handler */
                    if ((intflags & EIR_LINKIF) != 0) {
                        loop++;
                        if (is_link_up()) {
                            netif_set_link_up(&netif);
                        } else {
                            netif_set_link_down(&netif);
                        }

                        // check_link_status();
                        /* read PHIR to clear the flag */
                        read_phy(PHIR);
                    }

                    /* TX complete handler */
                    if (((intflags & EIR_TXIF) != 0) && ((intflags & EIR_TXERIF) == 0)) {                        
                        bool err = false;
                        loop++;
                        // ENC_DEBUG_print("intTX\n");
                        tx_retry_count = 0;
                        if (regb_read(ESTAT) & ESTAT_TXABRT) {
                            ENC_DEBUG_print("Tx Error (aborted)\n");
                            reset_tx_logic();
                            reg_bfclr(ESTAT, ESTAT_TXABRT);
                            LINK_STATS_INC(link.err);
                            err = true;
                        }
                        reg_bfclr(ECON1, ECON1_TXRTS);
                        reg_bfclr(EIR, EIR_TXIF);
                    }

                    /* TX Error handler */
                    if ((intflags & EIR_TXERIF) != 0) {
                        loop++;
                        uint8_t tsv[TSV_SIZE];
                        read_tsv(tsv);
                        dump_tsv("intTXErr", tsv);
                        LINK_STATS_INC(link.err);

                        reset_tx_logic();

                        /* Transmit Late collision check for retransmit */
                        if (TSV_GETBIT(tsv, TSV_TXLATECOLLISION)) {
                            ENC_DEBUG_print("LateCollision TXErr \n");
                            if (tx_retry_count++ < MAX_TX_RETRYCOUNT)
                                reg_bfset(ECON1, ECON1_TXRTS);
                            else
                                reg_bfclr(ECON1, ECON1_TXRTS);
                        } else
                            reg_bfclr(ECON1, ECON1_TXRTS);
                        reg_bfclr(EIR, EIR_TXERIF | EIR_TXIF);
                    }

                    /* RX Error handler */
                    if ((intflags & EIR_RXERIF) != 0) {
                        loop++;
                        ENC_DEBUG_print("intRXErr\n");
                        /* Check free FIFO space to flag RX overrun */
                        if (get_free_rxfifo() <= 0) {
                            ENC_DEBUG_print("RX Overrun\n");
                        }
                        reset_rx_logic();
                        LINK_STATS_INC(link.err);
                        reg_bfclr(EIR, EIR_RXERIF);
                    }

                    if (rx_interrupt()) {
                        loop++;
                    }
                    /* re-enable interrupts */
                    reg_bfset(EIE, EIE_INTIE);
                } while (loop);
            }
        }
    }

    extern "C" void enc28j60_irq_callback(uint gpio, uint32_t events) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        // Give a semaphore for irq_loop
        xSemaphoreGiveFromISR(irq_loop_sem, &xHigherPriorityTaskWoken);
        gpio_acknowledge_irq(gpio, events);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        return;
    }

    bool enc28j60::init() {
        config_.mutex = xSemaphoreCreateRecursiveMutex();
        config_.spi.init();
        config_.RST_gpio.output();
        config_.CS_gpio.output();

        config_.CS_gpio.set();
        config_.RST_gpio.reset();
        vTaskDelay(pdMS_TO_TICKS(AFTER_RESET_DELAY_MS));
        config_.RST_gpio.set();

        spi_write_op(ENC28J60_SOFT_RESET, 0x00, ENC28J60_SOFT_RESET);
        /* Errata workaround #1, CLKRDY check is unreliable,
         * delay at least 1 ms instead */
        vTaskDelay(pdMS_TO_TICKS(2));
        /* Oscillator ready */

        int retry = 10;
        while (!(regb_read(ESTAT) & ESTAT_CLKRDY)) {
            if (!(retry--)) {
                is_available = false;
                return is_available;
            }
        }

        rxfifo_init(RXSTART_INIT, RXEND_INIT);
        txfifo_init(TXSTART_INIT, TXEND_INIT);

        /* default filter mode: (unicast OR broadcast) AND crc valid */
        regb_write(ERXFCON, ERXFCON_UCEN | ERXFCON_CRCEN | ERXFCON_BCEN);

        /** Set the MARXEN bit in MACON1 to enable the MAC to receive frames. If using full duplex, most
         * applications should also set TXPAUS and RXPAUS to allow IEEE defined flow control to function
         */
        reg_bfset(MACON1, MACON1_MARXEN | MACON1_TXPAUS | MACON1_RXPAUS);
        /** Configure the PADCFG, TXCRCEN and FULDPX bits of MACON3. */
        // reg_bfset(MACON3, MACON3_PADCFG0 | MACON3_TXCRCEN | MACON3_FRMLNEN);

        if (full_duplex) {
            regb_write(MACON3, MACON3_PADCFG0 | MACON3_TXCRCEN | MACON3_FRMLNEN | MACON3_FULDPX);
            /* set inter-frame gap (non-back-to-back) */
            regb_write(MAIPG, 0x12);
            /* set inter-frame gap (back-to-back) */
            regb_write(MABBIPG, 0x15);
        } else {
            regb_write(MACON3, MACON3_PADCFG0 | MACON3_TXCRCEN | MACON3_FRMLNEN);
            regb_write(MACON4, 1 << 6); /* DEFER bit */
            /* set inter-frame gap (non-back-to-back) */
            regw_write(MAIPG, 0x0C12);
            /* set inter-frame gap (back-to-back) */
            regb_write(MABBIPG, 0x12);
        }

        /** Program the MAMXFL registers with the maxi- mum frame length to be permitted to be received
         * or transmitted. MAX PDU - offset */
        /*
         * MACLCON1 (default)
         * MACLCON2 (default)
         * Set the maximum packet size which the controller will accept.
         */
        regw_write(MAMXFL, MAX_FRAMELEN);

        /** Configure the Non-Back-to-Back Inter-Packet Gap register low byte, MAIPGL. Most applications
         * will program this register with 12h. If half duplex is used, the Non-Back-to-Back
         * Inter-Packet Gap register high byte, MAIPGH, should be programmed. Most applications will
         * program this register to 0Ch.*/
        regw_write(MAIPG, 0x0C12);

        if (full_duplex) {
            if (!write_phy(PHCON1, PHCON1_PDPXMD))
                return 0;
            if (!write_phy(PHCON2, 0x00))
                return 0;
            if (!write_phy(PHLCON, 0x0480))
                return 0;
        } else {
            if (!write_phy(PHCON1, 0x00))
                return 0;
            if (!write_phy(PHCON2, PHCON2_HDLDIS))
                return 0;
            if (!write_phy(PHLCON, ENC28J60_LAMPS_MODE))
                return 0;
        }

        flash_safe_execute([](void *me) { static_cast<enc28j60 *>(me)->generate_mac(); }, (void *)this, 100);

        regb_write(MAADR5, mac_[0]);
        regb_write(MAADR4, mac_[1]);
        regb_write(MAADR3, mac_[2]);
        regb_write(MAADR2, mac_[3]);
        regb_write(MAADR1, mac_[4]);
        regb_write(MAADR0, mac_[5]);

        // write_phy(PHCON2, PHCON2_HDLDIS);

        /** Start receiving */
        reg_bfset(EIE, EIE_INTIE | EIE_PKTIE | EIR_LINKIF);
        reg_bfset(ECON1, ECON1_RXEN);

        uint8_t rev = regb_read(EREVID);

        is_available = rev > 0;

        return is_available;
    }

    void enc28j60::enable_interupts() {
        // Enabling Interrupts

        lock();
        config_.IRQ_gpio.input();
        config_.IRQ_gpio.pull_up();
        taskENTER_CRITICAL();
        gpio_set_irq_enabled_with_callback(config_.IRQ_gpio.get_gpio(), GPIO_IRQ_EDGE_FALL, true, &enc28j60_irq_callback);

        taskEXIT_CRITICAL();

        write_phy(PHIE, PHIE_PGEIE | PHIE_PLNKIE);

        reg_bfclr(EIR, EIR_DMAIF | EIR_LINKIF | EIR_TXIF | EIR_TXERIF | EIR_RXERIF | EIR_PKTIF);
        regb_write(EIE, EIE_INTIE | EIE_PKTIE | EIE_LINKIE | EIE_TXIE | EIE_TXERIE | EIE_RXERIE);

        /* enable receive logic */
        reg_bfset(ECON1, ECON1_RXEN);

        unlock();
    }

    bool enc28j60::is_link_up() {
        return (read_phy(PHSTAT2) & PHSTAT2_LSTAT);
    }

    void enc28j60::spi_write_op(const uint8_t op, const uint8_t addr, const uint8_t data) {
        lock();
        config_.CS_gpio.reset();
        const uint8_t operation = op | (addr & ENC_ADDR_MASK);
        config_.spi.write(&operation, sizeof(operation));
        config_.spi.write(&data, sizeof(data));
        config_.CS_gpio.set();
        unlock();
    }

    uint8_t enc28j60::spi_read_op(const uint8_t op, const uint8_t reg) {
        lock();
        config_.CS_gpio.reset();
        const uint8_t operation = op | (reg & ENC_ADDR_MASK);
        uint8_t incoming_data{};

        config_.spi.write(&operation, 1);
        config_.spi.read(&incoming_data, 1);

        if (reg & 0x80) {
            /** @note If this is MAC register, then read dummy byte first */
            config_.spi.read(&incoming_data, 1);
        }

        config_.CS_gpio.set();
        unlock();
        return incoming_data;
    }

    /*
     * Register bit field Set
     */
    void enc28j60::reg_bfset(uint8_t addr, uint8_t mask) {
        select_bank(addr);
        spi_write_op(ENC28J60_BIT_FIELD_SET, addr, mask);
    }

    /*
     * Register bit field Clear
     */
    void enc28j60::reg_bfclr(uint8_t addr, uint8_t mask) {
        select_bank(addr);
        spi_write_op(ENC28J60_BIT_FIELD_CLR, addr, mask);
    }

    void enc28j60::select_bank(const uint8_t address) {
        /* These registers (EIE, EIR, ESTAT, ECON2, ECON1)
         * are present in all banks, no need to switch bank.
         */
        if (address >= EIE && address <= ECON1)
            return;

        if (current_register_bank != (address & BANK_MASK)) {
            spi_write_op(ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_BSEL0 | ECON1_BSEL1);
            spi_write_op(ENC28J60_BIT_FIELD_SET, ECON1, (address & BANK_MASK) >> 5);
            current_register_bank = address & BANK_MASK;
        }
    }

    void enc28j60::regb_write(const uint8_t addr, const uint8_t data) {
        lock();
        select_bank(addr);
        spi_write_op(ENC28J60_WRITE_CTRL_REG, addr, data);
        unlock();
    }

    void enc28j60::regw_write(const uint8_t addr, const uint16_t data) {
        //    enc28j60::write_op_16bit(ENC28J60_WRITE_CTRL_REG, addr, data);
        lock();
        regb_write(addr, data & 0xff);
        regb_write(addr + 1, data >> 8);
        unlock();
    }

    uint8_t enc28j60::regb_read(const uint8_t reg) {
        lock();
        select_bank(reg);
        uint8_t res = spi_read_op(ENC28J60_READ_CTRL_REG, reg);
        unlock();
        return res;
    }

    uint16_t enc28j60::regw_read(const uint8_t reg) {
        lock();
        uint8_t res = regb_read(reg) + (regb_read(reg + 1) << 8);
        unlock();
        return res;
    }

    int enc28j60::write_phy(const uint8_t reg, const uint16_t data) {
        /** 1. Write the address of the PHY register to write to into the MIREGADR register. */
        regb_write(MIREGADR, reg);
        /** 2. Write the lower 8 bits of data to write into the MIWRL register. */
        regw_write(MIWR, data);
        /** 3. Write the upper 8 bits of data to write into the MIWRH register.
         * Writing to this register auto- matically begins the MIIM transaction, so it must be written
         * to after MIWRL. The MISTAT.BUSY bit becomes set. */

        int ret = wait_phy_ready();
        return ret;
    }

    uint16_t enc28j60::read_phy(const uint8_t reg) {
        /** 1. Write the address of the PHY register to read from into the MIREGADR register.  */
        enc28j60::regb_write(MIREGADR, reg);
        uint8_t xd = enc28j60::regb_read(MIREGADR);

        /** 2. Set the MICMD.MIIRD bit. The read operation begins and the MISTAT.BUSY bit is set. */
        enc28j60::regb_write(MICMD, MICMD_MIIRD);

        /** 3. Wait 10.24 μs. Poll the MISTAT.BUSY bit to be certain that the operation is complete.
         While busy, the host controller should not start any MIISCAN operations or write to the MIWRH
        register. When the MAC has obtained the register contents, the BUSY bit will clear itself.  */
        while (enc28j60::regb_read(MISTAT) & MISTAT_BUSY)
            ;

        /** 4. Clear the MICMD.MIIRD bit. */
        enc28j60::regb_write(MICMD, 0x00);

        /** 5. Read the desired data from the MIRDL and MIRDH registers. The order that these bytes are
         * accessed is unimportant. */
        uint8_t out_L = enc28j60::regb_read(MIRDL);
        uint8_t out_H = enc28j60::regb_read(MIRDH);
        return (out_H << 8) | out_L;
    }

    size_t enc28j60::read_buff(uint8_t *src, size_t len) {
        lock();
        config_.CS_gpio.reset();
        const uint8_t operation = ENC28J60_READ_BUF_MEM;
        config_.spi.write(&operation, 1);
        auto ret = config_.spi.read(src, len);
        config_.CS_gpio.set();
        unlock();
        return ret;
    }

    void enc28j60::write_buff(const uint8_t *src, size_t len) {
        lock();
        config_.CS_gpio.reset();
        const uint8_t operation = ENC28J60_WRITE_BUF_MEM;
        config_.spi.write(&operation, 1);
        config_.spi.write(src, len);
        config_.CS_gpio.set();
        unlock();
    }

    uint8_t enc28j60::get_number_of_packets() {
        uint8_t n = regb_read(EPKTCNT);
        return n;
    }

    size_t enc28j60::get_incoming_packet(PacketMetaInfo &info, uint8_t *dst, const size_t length) {

        if (info.next_packet_pointer > RXEND_INIT) {
            ENC_DEBUG_print("Invalid packet address!!\n");
            /* packet address corrupted */
            reset_rx_logic();
            LINK_STATS_INC(link.err);

            return 0;
        }

        // regw_write(ERDPT, info.next_packet_pointer);
        next_packet_pointer = info.next_packet_pointer;
        regw_write(ERXRDPT, info.next_packet_pointer);

        size_t bytes_read;
        if (dst != nullptr) {
            bytes_read = read_buff(dst, length);
        }

        reg_bfset(ECON2, ECON2_PKTDEC);

        return bytes_read;
    }

#define MAX_RETRIES       1000U

    bool enc28j60::send_pbuf(struct pbuf *p) {
        if ((TXSTART_INIT + p->tot_len) > TXEND_INIT) {
            ENC_DEBUG_print("%s(%d, %d) packet too big!\n");
            return false;
        }

        // Wait until last transmission has finished
        uint16_t count = 0;
        while ((regb_read(EIR) & (EIR_TXIF | EIR_TXERIF)) == 0 && ++count < MAX_RETRIES) {            
        }

        // Always reset transmit logic (Errata Issue 12)
        reg_bfset(ECON1, ECON1_TXRST);
        reg_bfclr(ECON1, ECON1_TXRST);
        reg_bfclr(EIR, EIR_TXERIF | EIR_TXIF);

        // Prepare new transmission
        regw_write(EWRPT, TXSTART_INIT);

        // Set the TXND pointer to correspond to the packet size given
        regw_write(ETXND, TXSTART_INIT + p->tot_len);

        // Write per-packet control byte
        spi_write_op(ENC28J60_WRITE_BUF_MEM, 0, 0x00);

        // Copy pbuf chain to transmit buffer (buffer position on ENC28J60 set to autoincrement with every byte)
        for (const pbuf *q = p; q != nullptr; q = q->next) {
            if (q->payload && q->len > 0) {

                // Copy the packet into the transmit buffer
                write_buff((uint8_t *)q->payload, q->len);
            }
        }

        // Initiate transmission
        reg_bfset(ECON1, ECON1_TXRTS);

        return true;
    }

    enc28j60::PacketMetaInfo enc28j60::get_incoming_packet_info() {
        PacketMetaInfo ret{};
        regw_write(ERDPT, next_packet_pointer);
        read_buff(reinterpret_cast<uint8_t *>(&ret), sizeof(PacketMetaInfo));

        return ret;
    }

    bool enc28j60::link_state_changed() {
        if (current_link_state != is_link_up()) {
            current_link_state = !current_link_state;
            return true;
        }

        return false;
    }

    int enc28j60::poll_ready(uint8_t reg, uint8_t mask, uint8_t val) {
        TickType_t start = xTaskGetTickCount();

        /* 20 msec timeout read */
        while ((regb_read(reg) & mask) != val) {
            if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(20)) {
                return -ETIMEDOUT;
            }
            asm("nop");
        }
        return 0;
    }

    /*
     * Wait until the PHY operation is complete.
     */
    int enc28j60::wait_phy_ready() {
        return poll_ready(MISTAT, MISTAT_BUSY, 0) ? 0 : 1;
    }

    void enc28j60::txfifo_init(uint16_t start, uint16_t end) {
        if (start > 0x1FFF || end > 0x1FFF || start > end) {
            ENC_DEBUG_print("%s(%d, %d) TXFIFO bad parameters!\n");
            // 		__func__, start, end);
            return;
        }
        /* set transmit buffer start + end */
        regw_write(ETXST, start); // ETXSTL
        regw_write(ETXND, end);   // ETXNDL
    }

    void enc28j60::reset_tx_logic() {
        lock();
        reg_bfset(ECON1, ECON1_TXRST);
        reg_bfclr(ECON1, ECON1_TXRST);
        txfifo_init(TXSTART_INIT, TXEND_INIT);
        unlock();
    }

    void enc28j60::rxfifo_init(uint16_t start, uint16_t end) {
        if (start > 0x1FFF || end > 0x1FFF || start > end) {
            ENC_DEBUG_print("%s(%d, %d) RXFIFO bad parameters!\n");
            // 		__func__, start, end);
            return;
        }
        /* set receive buffer start + end */
        next_packet_pointer = start;
        regw_write(ERXST, RXSTART_INIT);

        uint16_t erxrdpt = erxrdpt_workaround(next_packet_pointer, start, end);
        regw_write(ERXRDPT, erxrdpt);

        regw_write(ERXND, end);
    }

    void enc28j60::reset_rx_logic() {
        lock();
        reg_bfclr(ECON1, ECON1_RXEN);
        reg_bfset(ECON1, ECON1_RXRST);
        reg_bfclr(ECON1, ECON1_RXRST);
        rxfifo_init(RXSTART_INIT, RXEND_INIT);
        reg_bfclr(EIR, EIR_RXERIF);
        reg_bfset(ECON1, ECON1_RXEN);
        unlock();
    }

    /*
     * Calculate free space in RxFIFO
     */
    int enc28j60::get_free_rxfifo() {
        int epkcnt, erxst, erxnd, erxwr, erxrd;
        int free_space;

        lock();
        epkcnt = regb_read(EPKTCNT);
        if (epkcnt >= 255)
            free_space = -1;
        else {
            erxst = regw_read(ERXST);
            erxnd = regw_read(ERXND);
            erxwr = regw_read(ERXWRPT);
            erxrd = regw_read(ERXRDPT);

            if (erxwr > erxrd)
                free_space = (erxnd - erxst) - (erxwr - erxrd);
            else if (erxwr == erxrd)
                free_space = (erxnd - erxst);
            else
                free_space = erxrd - erxwr - 1;
        }
        unlock();
        // ENC_DEBUG_print("%s() free_space = %d\n", __func__, free_space);
        return free_space;
    }

    err_t enc28j60::eth_packet_output(struct netif *netif, struct pbuf *p) {
        LINK_STATS_INC(link.xmit);
        drivers::enc28j60 *me = static_cast<drivers::enc28j60 *>(netif->state);

        if (!me->send_pbuf(p)) {
            ENC_DEBUG_print("Cannot send packet of length %d\n", p->tot_len);
            return ERR_ABRT;
        }

        ENC_DEBUG_print("Sent packet with len %d[%d]!\n", p->len, p->tot_len);
        return ERR_OK;
    }

    err_t enc28j60::eth_netif_init(struct netif *netif) {
        drivers::enc28j60 *me = static_cast<drivers::enc28j60 *>(netif->state);

        netif->linkoutput = drivers::enc28j60::eth_packet_output;
        netif->output = etharp_output;
        netif->mtu = ETHERNET_MTU;
        netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP | NETIF_FLAG_MLD6;
        memcpy(netif->hwaddr, me->mac_, sizeof(netif->hwaddr));
        netif->hwaddr_len = sizeof(netif->hwaddr);

        ENC_DEBUG_print("LWIP Init \n");

        return ERR_OK;
    }

    /*
     * Read the Transmit Status Vector
     */
    void enc28j60::read_tsv(uint8_t tsv[TSV_SIZE]) {
        int16_t endptr = regw_read(ETXND);
        // ENC_DEBUG_print("enc28j60: reading TSV at addr:0x%04x\n", endptr + 1);
        regw_write(ERDPT, endptr + 1);
        read_buff(tsv, TSV_SIZE);
    }

    void enc28j60::dump_tsv(const char *msg, uint8_t tsv[TSV_SIZE]) {
        uint16_t tmp1, tmp2;

        ENC_DEBUG_print("enc28j60: %s - TSV:\n", msg);
        tmp1 = tsv[1];
        tmp1 <<= 8;
        tmp1 |= tsv[0];

        tmp2 = tsv[5];
        tmp2 <<= 8;
        tmp2 |= tsv[4];

        ENC_DEBUG_print(
            "enc28j60: ByteCount: %d, CollisionCount: %d,"
            " TotByteOnWire: %d\n",
            tmp1,
            tsv[2] & 0x0f,
            tmp2);
        ENC_DEBUG_print(
            "enc28j60: TxDone: %d, CRCErr:%d, LenChkErr: %d,"
            " LenOutOfRange: %d\n",
            TSV_GETBIT(tsv, TSV_TXDONE),
            TSV_GETBIT(tsv, TSV_TXCRCERROR),
            TSV_GETBIT(tsv, TSV_TXLENCHKERROR),
            TSV_GETBIT(tsv, TSV_TXLENOUTOFRANGE));
        ENC_DEBUG_print(
            "enc28j60: Multicast: %d, Broadcast: %d, "
            "PacketDefer: %d, ExDefer: %d\n",
            TSV_GETBIT(tsv, TSV_TXMULTICAST),
            TSV_GETBIT(tsv, TSV_TXBROADCAST),
            TSV_GETBIT(tsv, TSV_TXPACKETDEFER),
            TSV_GETBIT(tsv, TSV_TXEXDEFER));
        ENC_DEBUG_print(
            "enc28j60: ExCollision: %d, LateCollision: %d, "
            "Giant: %d, Underrun: %d\n",
            TSV_GETBIT(tsv, TSV_TXEXCOLLISION),
            TSV_GETBIT(tsv, TSV_TXLATECOLLISION),
            TSV_GETBIT(tsv, TSV_TXGIANT),
            TSV_GETBIT(tsv, TSV_TXUNDERRUN));
        ENC_DEBUG_print(
            "enc28j60: ControlFrame: %d, PauseFrame: %d, "
            "BackPressApp: %d, VLanTagFrame: %d\n",
            TSV_GETBIT(tsv, TSV_TXCONTROLFRAME),
            TSV_GETBIT(tsv, TSV_TXPAUSEFRAME),
            TSV_GETBIT(tsv, TSV_BACKPRESSUREAPP),
            TSV_GETBIT(tsv, TSV_TXVLANTAGFRAME));
    }

    // Generate a locally administered MAC address

    void enc28j60::generate_mac() {
        uint8_t id[FLASH_UNIQUE_ID_SIZE_BYTES];

        // Disable interrupts on the current core
        uint32_t status = save_and_disable_interrupts();
        flash_get_unique_id(id); // unique ID from Pico's flash

        // Re-enable interrupts on the current core
        restore_interrupts(status);

        // Use some bytes from unique ID
        mac_[0] = 0x02; // Locally administered, unicast
        mac_[1] = id[0];
        mac_[2] = id[1];
        mac_[3] = id[2];
        mac_[4] = id[3];
        mac_[5] = id[4];
    }

} // namespace drivers
