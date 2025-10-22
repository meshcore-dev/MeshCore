/// <reference types="node" />
/// <reference types="node" />
/// <reference types="node" />
import { Transform, TransformCallback, TransformOptions } from 'stream';
export interface InterByteTimeoutOptions extends TransformOptions {
    /** the period of silence in milliseconds after which data is emitted */
    interval: number;
    /** the maximum number of bytes after which data will be emitted. Defaults to 65536 */
    maxBufferSize?: number;
}
/**
 * A transform stream that buffers data and emits it after not receiving any bytes for the specified amount of time or hitting a max buffer size.
 */
export declare class InterByteTimeoutParser extends Transform {
    maxBufferSize: number;
    currentPacket: number[];
    interval: number;
    intervalID: NodeJS.Timeout | undefined;
    constructor({ maxBufferSize, interval, ...transformOptions }: InterByteTimeoutOptions);
    _transform(chunk: Buffer, encoding: BufferEncoding, cb: TransformCallback): void;
    emitPacket(): void;
    _flush(cb: TransformCallback): void;
}
