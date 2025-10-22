"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.WindowsPortBinding = exports.WindowsBinding = void 0;
const debug_1 = __importDefault(require("debug"));
const _1 = require(".");
const load_bindings_1 = require("./load-bindings");
const win32_sn_parser_1 = require("./win32-sn-parser");
const debug = (0, debug_1.default)('serialport/bindings-cpp');
exports.WindowsBinding = {
    async list() {
        const ports = await (0, load_bindings_1.asyncList)();
        // Grab the serial number from the pnp id
        return ports.map(port => {
            if (port.pnpId && !port.serialNumber) {
                const serialNumber = (0, win32_sn_parser_1.serialNumParser)(port.pnpId);
                if (serialNumber) {
                    return Object.assign(Object.assign({}, port), { serialNumber });
                }
            }
            return port;
        });
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
        const openOptions = Object.assign({ dataBits: 8, lock: true, stopBits: 1, parity: 'none', rtscts: false, rtsMode: 'handshake', xon: false, xoff: false, xany: false, hupcl: true }, options);
        const fd = await (0, load_bindings_1.asyncOpen)(openOptions.path, openOptions);
        return new WindowsPortBinding(fd, openOptions);
    },
};
/**
 * The Windows binding layer
 */
class WindowsPortBinding {
    constructor(fd, options) {
        this.fd = fd;
        this.openOptions = options;
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
        try {
            const bytesRead = await (0, load_bindings_1.asyncRead)(this.fd, buffer, offset, length);
            return { bytesRead, buffer };
        }
        catch (err) {
            if (!this.isOpen) {
                throw new _1.BindingsError(err.message, { canceled: true });
            }
            throw err;
        }
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
            await (0, load_bindings_1.asyncWrite)(this.fd, buffer);
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
        return (0, load_bindings_1.asyncGetBaudRate)(this.fd);
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
exports.WindowsPortBinding = WindowsPortBinding;
