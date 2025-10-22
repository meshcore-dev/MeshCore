"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __exportStar = (this && this.__exportStar) || function(m, exports) {
    for (var p in m) if (p !== "default" && !Object.prototype.hasOwnProperty.call(exports, p)) __createBinding(exports, m, p);
};
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.autoDetect = void 0;
/* eslint-disable @typescript-eslint/no-var-requires */
const debug_1 = __importDefault(require("debug"));
const darwin_1 = require("./darwin");
const linux_1 = require("./linux");
const win32_1 = require("./win32");
const debug = (0, debug_1.default)('serialport/bindings-cpp');
__exportStar(require("@serialport/bindings-interface"), exports);
__exportStar(require("./darwin"), exports);
__exportStar(require("./linux"), exports);
__exportStar(require("./win32"), exports);
__exportStar(require("./errors"), exports);
/**
 * This is an auto detected binding for your current platform
 */
function autoDetect() {
    switch (process.platform) {
        case 'win32':
            debug('loading WindowsBinding');
            return win32_1.WindowsBinding;
        case 'darwin':
            debug('loading DarwinBinding');
            return darwin_1.DarwinBinding;
        default:
            debug('loading LinuxBinding');
            return linux_1.LinuxBinding;
    }
}
exports.autoDetect = autoDetect;
