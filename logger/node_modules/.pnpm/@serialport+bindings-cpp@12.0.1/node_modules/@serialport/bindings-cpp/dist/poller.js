"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.Poller = exports.EVENTS = void 0;
const debug_1 = __importDefault(require("debug"));
const events_1 = require("events");
const path_1 = require("path");
const node_gyp_build_1 = __importDefault(require("node-gyp-build"));
const errors_1 = require("./errors");
const { Poller: PollerBindings } = (0, node_gyp_build_1.default)((0, path_1.join)(__dirname, '../'));
const logger = (0, debug_1.default)('serialport/bindings-cpp/poller');
exports.EVENTS = {
    UV_READABLE: 0b0001,
    UV_WRITABLE: 0b0010,
    UV_DISCONNECT: 0b0100,
};
function handleEvent(error, eventFlag) {
    if (error) {
        logger('error', error);
        this.emit('readable', error);
        this.emit('writable', error);
        this.emit('disconnect', error);
        return;
    }
    if (eventFlag & exports.EVENTS.UV_READABLE) {
        logger('received "readable"');
        this.emit('readable', null);
    }
    if (eventFlag & exports.EVENTS.UV_WRITABLE) {
        logger('received "writable"');
        this.emit('writable', null);
    }
    if (eventFlag & exports.EVENTS.UV_DISCONNECT) {
        logger('received "disconnect"');
        this.emit('disconnect', null);
    }
}
/**
 * Polls unix systems for readable or writable states of a file or serialport
 */
class Poller extends events_1.EventEmitter {
    constructor(fd, FDPoller = PollerBindings) {
        logger('Creating poller');
        super();
        this.poller = new FDPoller(fd, handleEvent.bind(this));
    }
    /**
     * Wait for the next event to occur
     * @param {string} event ('readable'|'writable'|'disconnect')
     * @returns {Poller} returns itself
     */
    once(event, callback) {
        switch (event) {
            case 'readable':
                this.poll(exports.EVENTS.UV_READABLE);
                break;
            case 'writable':
                this.poll(exports.EVENTS.UV_WRITABLE);
                break;
            case 'disconnect':
                this.poll(exports.EVENTS.UV_DISCONNECT);
                break;
        }
        return super.once(event, callback);
    }
    /**
     * Ask the bindings to listen for an event, it is recommend to use `.once()` for easy use
     * @param {EVENTS} eventFlag polls for an event or group of events based upon a flag.
     */
    poll(eventFlag = 0) {
        if (eventFlag & exports.EVENTS.UV_READABLE) {
            logger('Polling for "readable"');
        }
        if (eventFlag & exports.EVENTS.UV_WRITABLE) {
            logger('Polling for "writable"');
        }
        if (eventFlag & exports.EVENTS.UV_DISCONNECT) {
            logger('Polling for "disconnect"');
        }
        this.poller.poll(eventFlag);
    }
    /**
     * Stop listening for events and cancel all outstanding listening with an error
     */
    stop() {
        logger('Stopping poller');
        this.poller.stop();
        this.emitCanceled();
    }
    destroy() {
        logger('Destroying poller');
        this.poller.destroy();
        this.emitCanceled();
    }
    emitCanceled() {
        const err = new errors_1.BindingsError('Canceled', { canceled: true });
        this.emit('readable', err);
        this.emit('writable', err);
        this.emit('disconnect', err);
    }
}
exports.Poller = Poller;
