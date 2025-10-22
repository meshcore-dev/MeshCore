"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.InterByteTimeoutParser = void 0;
const stream_1 = require("stream");
/**
 * A transform stream that buffers data and emits it after not receiving any bytes for the specified amount of time or hitting a max buffer size.
 */
class InterByteTimeoutParser extends stream_1.Transform {
    maxBufferSize;
    currentPacket;
    interval;
    intervalID;
    constructor({ maxBufferSize = 65536, interval, ...transformOptions }) {
        super(transformOptions);
        if (!interval) {
            throw new TypeError('"interval" is required');
        }
        if (typeof interval !== 'number' || Number.isNaN(interval)) {
            throw new TypeError('"interval" is not a number');
        }
        if (interval < 1) {
            throw new TypeError('"interval" is not greater than 0');
        }
        if (typeof maxBufferSize !== 'number' || Number.isNaN(maxBufferSize)) {
            throw new TypeError('"maxBufferSize" is not a number');
        }
        if (maxBufferSize < 1) {
            throw new TypeError('"maxBufferSize" is not greater than 0');
        }
        this.maxBufferSize = maxBufferSize;
        this.currentPacket = [];
        this.interval = interval;
    }
    _transform(chunk, encoding, cb) {
        if (this.intervalID) {
            clearTimeout(this.intervalID);
        }
        for (let offset = 0; offset < chunk.length; offset++) {
            this.currentPacket.push(chunk[offset]);
            if (this.currentPacket.length >= this.maxBufferSize) {
                this.emitPacket();
            }
        }
        this.intervalID = setTimeout(this.emitPacket.bind(this), this.interval);
        cb();
    }
    emitPacket() {
        if (this.intervalID) {
            clearTimeout(this.intervalID);
        }
        if (this.currentPacket.length > 0) {
            this.push(Buffer.from(this.currentPacket));
        }
        this.currentPacket = [];
    }
    _flush(cb) {
        this.emitPacket();
        cb();
    }
}
exports.InterByteTimeoutParser = InterByteTimeoutParser;
