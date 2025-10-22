"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.unixRead = void 0;
const util_1 = require("util");
const fs_1 = require("fs");
const errors_1 = require("./errors");
const debug_1 = __importDefault(require("debug"));
const logger = (0, debug_1.default)('serialport/bindings-cpp/unixRead');
const readAsync = (0, util_1.promisify)(fs_1.read);
const readable = (binding) => {
    return new Promise((resolve, reject) => {
        if (!binding.poller) {
            throw new Error('No poller on bindings');
        }
        binding.poller.once('readable', err => (err ? reject(err) : resolve()));
    });
};
const unixRead = async ({ binding, buffer, offset, length, fsReadAsync = readAsync, }) => {
    logger('Starting read');
    if (!binding.isOpen || !binding.fd) {
        throw new errors_1.BindingsError('Port is not open', { canceled: true });
    }
    try {
        const { bytesRead } = await fsReadAsync(binding.fd, buffer, offset, length, null);
        if (bytesRead === 0) {
            return (0, exports.unixRead)({ binding, buffer, offset, length, fsReadAsync });
        }
        logger('Finished read', bytesRead, 'bytes');
        return { bytesRead, buffer };
    }
    catch (err) {
        logger('read error', err);
        if (err.code === 'EAGAIN' || err.code === 'EWOULDBLOCK' || err.code === 'EINTR') {
            if (!binding.isOpen) {
                throw new errors_1.BindingsError('Port is not open', { canceled: true });
            }
            logger('waiting for readable because of code:', err.code);
            await readable(binding);
            return (0, exports.unixRead)({ binding, buffer, offset, length, fsReadAsync });
        }
        const disconnectError = err.code === 'EBADF' || // Bad file number means we got closed
            err.code === 'ENXIO' || // No such device or address probably usb disconnect
            err.code === 'UNKNOWN' ||
            err.errno === -1; // generic error
        if (disconnectError) {
            err.disconnect = true;
            logger('disconnecting', err);
        }
        throw err;
    }
};
exports.unixRead = unixRead;
