/// <reference types="node" />
/// <reference types="node" />
import { Transform, TransformCallback, TransformOptions } from 'stream';
export interface SlipEncoderOptions extends TransformOptions {
    /** Custom start byte */
    START?: number;
    /** Custom start escape byte */
    ESC_START?: number;
    /** custom escape byte */
    ESC?: number;
    /** custom end byte */
    END?: number;
    /** custom escape end byte */
    ESC_END?: number;
    /** custom escape escape byte */
    ESC_ESC?: number;
    /** Adds an END character at the beginning of each packet per the Bluetooth Core Specification 4.0, Volume 4, Part D, Chapter 3 "SLIP Layer" and allowed by RFC 1055 */
    bluetoothQuirk?: boolean;
}
/**
 * A transform stream that emits SLIP-encoded data for each incoming packet.
 *
 * Runs in O(n) time, adding a 0xC0 character at the end of each
 * received packet and escaping characters, according to RFC 1055.
 */
export declare class SlipEncoder extends Transform {
    opts: {
        START: number | undefined;
        ESC: number;
        END: number;
        ESC_START: number | undefined;
        ESC_END: number;
        ESC_ESC: number;
        bluetoothQuirk: boolean;
    };
    constructor(options?: SlipEncoderOptions);
    _transform(chunk: Buffer, encoding: BufferEncoding, cb: TransformCallback): void;
}
