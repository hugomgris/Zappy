#pragma once

#include "../net/WebsocketClient.hpp"

class Sender {
    private:
    WebsocketClient     _ws;

    public:
        Sender(WebsocketClient& ws);
};