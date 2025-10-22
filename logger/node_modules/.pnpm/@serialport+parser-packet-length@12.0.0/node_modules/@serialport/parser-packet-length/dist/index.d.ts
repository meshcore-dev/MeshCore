/// <reference types="node" />
/// <reference types="node" />
import { Transform, TransformCallback, TransformOptions } from 'stream';
export interface PacketLengthOptions extends TransformOptions {
    /** delimiter to use defaults to 0xaa */
    delimiter?: number;
    /** overhead of packet (including length, delimiter and any checksum / packet footer) defaults to 2 */
    packetOverhead?: number;
    /** number of bytes containing length defaults to 1 */
    lengthBytes?: number;
    /** offset of length field defaults to 1 */
    lengthOffset?: number;
    /**  max packet length defaults to 0xff */
    maxLen?: number;
}
/**
 * A transform stream that decodes packets with a delimiter and length of payload
 * specified within the data stream.
 * @extends Transform
 * @summary Decodes packets of the general form:
 *       [delimiter][len][payload0] ... [payload0 + len]
 *
 * The length field can be up to 4 bytes and can be at any offset within the packet
 *       [delimiter][header0][header1][len0][len1[payload0] ... [payload0 + len]
 *
 * The offset and number of bytes of the length field need to be provided in options
 * if not 1 byte immediately following the delimiter.
 */
export declare class PacketLengthParser extends Transform {
    buffer: Buffer;
    start: boolean;
    opts: {
        delimiter: number;
        packetOverhead: number;
        lengthBytes: number;
        lengthOffset: number;
        maxLen: number;
    };
    constructor(options?: PacketLengthOptions);
    _transform(chunk: Buffer, encoding: BufferEncoding, cb: TransformCallback): void;
    _flush(cb: TransformCallback): void;
}
