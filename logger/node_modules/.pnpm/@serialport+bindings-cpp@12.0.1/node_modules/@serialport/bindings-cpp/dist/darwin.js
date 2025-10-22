"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.DarwinPortBinding = exports.DarwinBinding = void 0;
const debug_1 = __importDefault(require("debug"));
const load_bindings_1 = require("./load-bindings");
const poller_1 = require("./poller");
const unix_read_1 = require("./unix-read");
const unix_write_1 = require("./unix-write");
const debug = (0, debug_1.default)('serialport/bindings-cpp');
exports.DarwinBinding = {
    list() {
        debug('list');
        return (0, load_bindings_1.asyncList)();
    },
    async open(options) {
        if (!options || typeof options !== 'object' || Array.isArray(options)) {
            throw new TypeError('"options" is not an object');
        }
        if (!options.path) {
            throw new TypeError('"path" is not a valid port');
        }
        if (!options.baudRate) {
            throw new TypeError('"baudRate" is not a valid baudRate');
        }
        debug('open');
        const openOptions = Object.assign({ vmin: 1, vtime: 0, dataBits: 8, lock: true, stopBits: 1, parity: 'none', rtscts: false, xon: false, xoff: false, xany: false, hupcl: true }, options);
        const fd = await (0, load_bindings_1.asyncOpen)(openOptions.path, openOptions);
        return new DarwinPortBinding(fd, openOptions);
    },
};
/**
 * The Darwin binding layer for OSX
 */
class DarwinPortBinding {
    constructor(fd, options) {
        this.fd = fd;
        this.openOptions = options;
        this.poller = new poller_1.Poller(fd);
        this.writeOperation = null;
    }
    get isOpen() {
        return this.fd !== null;
    }
    async close() {
        debug('close');
        if (!this.isOpen) {
            throw new Error('Port is not open');
        }
        const fd = this.fd;
        this.poller.stop();
        this.poller.destroy();
        this.fd = null;
        await (0, load_bindings_1.asyncClose)(fd);
    }
    async read(buffer, offset, length) {
        if (!Buffer.isBuffer(buffer)) {
            throw new TypeError('"buffer" is not a Buffer');
        }
        if (typeof offset !== 'number' || isNaN(offset)) {
            throw new TypeError(`"offset" is not an integer got "${isNaN(offset) ? 'NaN' : typeof offset}"`);
        }
        if (typeof length !== 'number' || isNaN(length)) {
            throw new TypeError(`"length" is not an integer got "${isNaN(length) ? 'NaN' : typeof length}"`);
        }
        debug('read');
        if (buffer.length < offset + length) {
            throw new Error('buffer is too small');
        }
        if (!this.isOpen) {
            throw new Error('Port is not open');
        }
        return (0, unix_read_1.unixRead)({ binding: this, buffer, offset, length });
    }
    async write(buffer) {
        if (!Buffer.isBuffer(buffer)) {
            throw new TypeError('"buffer" is not a Buffer');
        }
        debug('write', buffer.length, 'bytes');
        if (!this.isOpen) {
            debug('write', 'error port is not open');
            throw new Error('Port is not open');
        }
        this.writeOperation = (async () => {
            if (buffer.length === 0) {
                return;
            }
            await (0, unix_write_1.unixWrite)({ binding: this, buffer });
            this.writeOperation = null;
        })();
        return this.writeOperation;
    }
    async update(options) {
        if (!options || typeof options !== 'object' || Array.isArray(options)) {
            throw TypeError('"options" is not an object');
        }
        if (typeof options.baudRate !== 'number') {
            throw new TypeError('"options.baudRate" is not a number');
        }
        debug('update');
        if (!this.isOpen) {
            throw new Error('Port is not open');
        }
        await (0, load_bindings_1.asyncUpdate)(this.fd, options);
    }
    async set(options) {
        if (!options || typeof options !== 'object' || Array.isArray(options)) {
            throw new TypeError('"options" is not an object');
        }
        debug('set', options);
        if (!this.isOpen) {
            throw new Error('Port is not open');
        }
        await (0, load_bindings_1.asyncSet)(this.fd, options);
    }
    async get() {
        debug('get');
        if (!this.isOpen) {
            throw new Error('Port is not open');
        }
        return (0, load_bindings_1.asyncGet)(this.fd);
    }
    async getBaudRate() {
        debug('getBaudRate');
        if (!this.isOpen) {
            throw new Error('Port is not open');
        }
        throw new Error('getBaudRate is not implemented on darwin');
    }
    async flush() {
        debug('flush');
        if (!this.isOpen) {
            throw new Error('Port is not open');
        }
        await (0, load_bindings_1.asyncFlush)(this.fd);
    }
    async drain() {
        debug('drain');
        if (!this.isOpen) {
            throw new Error('Port is not open');
        }
        await this.writeOperation;
        await (0, load_bindings_1.asyncDrain)(this.fd);
    }
}
exports.DarwinPortBinding = DarwinPortBinding;
