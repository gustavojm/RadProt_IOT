#pragma once

#include "tcp_server.h"
#include "arduinojson_cust_alloc.h"

#include <cstdint>

namespace json = ArduinoJson;

class tcp_server_command : public tcp_server {

  public:
    tcp_server_command(int port) : tcp_server("command", port, 3) {
    }

    json::MyJsonDocument led_toggle_cmd(json::JsonObject const pars);
    json::MyJsonDocument cmd_execute(char const *cmd, json::JsonObject const pars);

    bool reply_fn(int conn_sock) override;

    // FredMemFn points to a member of Fred that takes (char,float)
    typedef json::MyJsonDocument (tcp_server_command::*cmd_function_ptr)(json::JsonObject pars);

    typedef struct {
        const char *cmd_name;
        cmd_function_ptr cmd_function;
    } cmd_entry;

    static const cmd_entry cmds_table[];
};
