"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.linuxList = void 0;
const child_process_1 = require("child_process");
const parser_readline_1 = require("@serialport/parser-readline");
// get only serial port names
function checkPathOfDevice(path) {
    return /(tty(S|WCH|ACM|USB|AMA|MFD|O|XRUSB)|rfcomm)/.test(path) && path;
}
function propName(name) {
    return {
        DEVNAME: 'path',
        ID_VENDOR_ENC: 'manufacturer',
        ID_SERIAL_SHORT: 'serialNumber',
        ID_VENDOR_ID: 'vendorId',
        ID_MODEL_ID: 'productId',
        DEVLINKS: 'pnpId',
        /**
        * Workaround for systemd defect
        * see https://github.com/serialport/bindings-cpp/issues/115
        */
        ID_USB_VENDOR_ENC: 'manufacturer',
        ID_USB_SERIAL_SHORT: 'serialNumber',
        ID_USB_VENDOR_ID: 'vendorId',
        ID_USB_MODEL_ID: 'productId',
        // End of workaround
    }[name.toUpperCase()];
}
function decodeHexEscape(str) {
    return str.replace(/\\x([a-fA-F0-9]{2})/g, (a, b) => {
        return String.fromCharCode(parseInt(b, 16));
    });
}
function propVal(name, val) {
    if (name === 'pnpId') {
        const match = val.match(/\/by-id\/([^\s]+)/);
        return (match === null || match === void 0 ? void 0 : match[1]) || undefined;
    }
    if (name === 'manufacturer') {
        return decodeHexEscape(val);
    }
    if (/^0x/.test(val)) {
        return val.substr(2);
    }
    return val;
}
function linuxList(spawnCmd = child_process_1.spawn) {
    const ports = [];
    const udevadm = spawnCmd('udevadm', ['info', '-e']);
    const lines = udevadm.stdout.pipe(new parser_readline_1.ReadlineParser());
    let skipPort = false;
    let port = {
        path: '',
        manufacturer: undefined,
        serialNumber: undefined,
        pnpId: undefined,
        locationId: undefined,
        vendorId: undefined,
        productId: undefined,
    };
    lines.on('data', (line) => {
        const lineType = line.slice(0, 1);
        const data = line.slice(3);
        // new port entry
        if (lineType === 'P') {
            port = {
                path: '',
                manufacturer: undefined,
                serialNumber: undefined,
                pnpId: undefined,
                locationId: undefined,
                vendorId: undefined,
                productId: undefined,
            };
            skipPort = false;
            return;
        }
        if (skipPort) {
            return;
        }
        // Check dev name and save port if it matches flag to skip the rest of the data if not
        if (lineType === 'N') {
            if (checkPathOfDevice(data)) {
                ports.push(port);
            }
            else {
                skipPort = true;
            }
            return;
        }
        // parse data about each port
        if (lineType === 'E') {
            const keyValue = data.match(/^(.+)=(.*)/);
            if (!keyValue) {
                return;
            }
            const key = propName(keyValue[1]);
            if (!key) {
                return;
            }
            port[key] = propVal(key, keyValue[2]);
        }
    });
    return new Promise((resolve, reject) => {
        udevadm.on('close', (code) => {
            if (code) {
                reject(new Error(`Error listing ports udevadm exited with error code: ${code}`));
            }
        });
        udevadm.on('error', reject);
        lines.on('error', reject);
        lines.on('finish', () => resolve(ports));
    });
}
exports.linuxList = linuxList;
