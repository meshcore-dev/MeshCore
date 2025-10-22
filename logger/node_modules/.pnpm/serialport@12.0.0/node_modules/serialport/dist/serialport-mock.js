"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.SerialPortMock = void 0;
const stream_1 = require("@serialport/stream");
const binding_mock_1 = require("@serialport/binding-mock");
class SerialPortMock extends stream_1.SerialPortStream {
    static list = binding_mock_1.MockBinding.list;
    static binding = binding_mock_1.MockBinding;
    constructor(options, openCallback) {
        const opts = {
            binding: binding_mock_1.MockBinding,
            ...options,
        };
        super(opts, openCallback);
    }
}
exports.SerialPortMock = SerialPortMock;
