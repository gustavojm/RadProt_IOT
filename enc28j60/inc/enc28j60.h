#pragma once

/**
 * @note This driver bases on EtherCard repository.
 * License: GPL-2.0 license
 * URL: https://github.com/njh/EtherCard
 */

#include "gpio.h"
#include "spi.h"
#include <array>
#include <cinttypes>

#include "FreeRTOS.h"
#include "enc28j60_registers.h"
#include "lwip/err.h"
#include "lwip/netif.h"
#include "semphr.h"

#include <cinttypes>

inline SemaphoreHandle_t irq_loop_sem;

#define TSV_BYTEOF(x)    ((x) / 8)
#define TSV_BITMASK(x)   (1 << ((x) % 8))
#define TSV_GETBIT(x, y) (((x)[TSV_BYTEOF(y)] & TSV_BITMASK(y)) ? 1 : 0)

namespace drivers {

    class enc28j60 {
        /* buffer boundaries applied to internal 8K ram
        * entire available packet buffer space is allocated.

        * Give TX buffer space for one full ethernet frame (~1500 bytes)
        * receive buffer gets the rest */
        static constexpr uint16_t TXSTART_INIT = 0x1A00;
        static constexpr uint16_t TXEND_INIT = 0x1FFF;

        /* Put RX buffer at 0 as suggested by the Errata datasheet */
        static constexpr uint16_t RXSTART_INIT = 0x0;
        static constexpr uint16_t RXEND_INIT = 0x19FF;

        static constexpr uint32_t AFTER_RESET_DELAY_MS = 100;
        static constexpr size_t ETHERNET_MTU = 1500;
        static constexpr uint8_t MAX_TX_RETRYCOUNT = 16;

        /* maximum ethernet frame length */
        static constexpr uint16_t MAX_FRAMELEN = 1518;

      public:
        struct Config {
            Gpio CS_gpio;
            Gpio RST_gpio;
            Gpio IRQ_gpio;
            Spi &spi;
        };

        SemaphoreHandle_t mutex;
        // Set the interface on linux machine: sudo ethtool -s enp1s0 autoneg off speed 10 duplex full
        // view links status: dmesg |grep <iface>
        bool full_duplex = false;

        struct __attribute__((packed)) PacketMetaInfo {
            uint16_t next_packet_pointer;
            uint16_t byte_count;
            uint16_t long_drop_event : 1;
            uint16_t reserved : 1;
            uint16_t carrier_event_previously_seen : 1;
            uint16_t reserved_2 : 1;
            uint16_t crc_err : 1;
            uint16_t length_check_err : 1;
            uint16_t length_out_of_range : 1;
            uint16_t received_ok : 1;
            uint16_t receive_multicast_packet : 1;
            uint16_t receive_broadcast_packet : 1;
            uint16_t dribble_nibble : 1;
            uint16_t receive_control_frame : 1;
            uint16_t receive_pause_control_frame : 1;
            uint16_t receive_unknown_opcode : 1;
            uint16_t receive_vlan_type_detected : 1;
            uint16_t zero : 1;
        };

        enc28j60(Config config);

      private:
        void lock();
        void unlock();

        int rx_interrupt();

        size_t get_incoming_packet(PacketMetaInfo &info, uint8_t *dst, const size_t max_length);
        PacketMetaInfo get_incoming_packet_info();
        
        bool send_pbuf(struct pbuf *p);

        bool link_state_changed();
        void __not_in_flash_func(generate_mac)();

        Config config_;

        void spi_write_op(uint8_t operation, const uint8_t reg, const uint8_t data);
        uint8_t spi_read_op(uint8_t operation, const uint8_t reg);
        void set_bank(const uint8_t address);

        void regb_write(const uint8_t addr, const uint8_t data);
        void regw_write(const uint8_t addr, const uint16_t data);

        uint8_t regb_read(const uint8_t reg);
        uint16_t regw_read(const uint8_t reg);

        int write_phy(const uint8_t reg, const uint16_t data);
        uint16_t read_phy(const uint8_t reg);

        size_t read_buff(uint8_t *dst, size_t len);
        void write_buff(const uint8_t *src, size_t len);

        uint8_t current_register_bank;
        uint16_t next_packet_pointer;
        bool current_link_state;
        void read_tsv(uint8_t tsv[TSV_SIZE]);
        void dump_tsv(const char *msg, uint8_t tsv[TSV_SIZE]);

        int wait_phy_ready();
        int poll_ready(uint8_t reg, uint8_t mask, uint8_t val);
        void txfifo_init(uint16_t start, uint16_t end);
        void reset_tx_logic();
        void rxfifo_init(uint16_t start, uint16_t end);
        void reg_bfset(uint8_t addr, uint8_t mask);
        void reg_bfclr(uint8_t addr, uint8_t mask);
        void reset_rx_logic();
        int get_free_rxfifo();
        static err_t eth_packet_output(struct netif *netif, struct pbuf *p);
        uint16_t tx_retry_count = 0;

      public:
        using MacAddress = uint8_t[6];

        struct netif netif{};
        bool is_available = true;
        MacAddress mac_;

        bool init();
        bool is_link_up();
        void irq_deferred_handler();
        void enable_interupts();
        static err_t eth_netif_init(struct netif *netif);
    };

} // namespace drivers
