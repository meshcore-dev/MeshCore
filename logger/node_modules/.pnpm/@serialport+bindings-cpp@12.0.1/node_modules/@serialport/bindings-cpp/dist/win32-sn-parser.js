"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.serialNumParser = void 0;
const PARSERS = [/USB\\(?:.+)\\(.+)/, /FTDIBUS\\(?:.+)\+(.+?)A?\\.+/];
const serialNumParser = (pnpId) => {
    if (!pnpId) {
        return null;
    }
    for (const parser of PARSERS) {
        const sn = pnpId.match(parser);
        if (sn) {
            return sn[1];
        }
    }
    return null;
};
exports.serialNumParser = serialNumParser;
