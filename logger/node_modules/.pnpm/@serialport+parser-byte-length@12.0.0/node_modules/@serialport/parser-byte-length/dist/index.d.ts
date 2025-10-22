/// <reference types="node" />
/// <reference types="node" />
import { Transform, TransformCallback, TransformOptions } from 'stream';
export interface ByteLengthOptions extends TransformOptions {
    /** the number of bytes on each data event */
    length: number;
}
/**
 * Emit data every number of bytes
 *
 * A transform stream that emits data as a buffer after a specific number of bytes are received. Runs in O(n) time.
 */
export declare class ByteLengthParser extends Transform {
    length: number;
    private position;
    private buffer;
    constructor(options: ByteLengthOptions);
    _transform(chunk: Buffer, _encoding: BufferEncoding, cb: TransformCallback): void;
    _flush(cb: TransformCallback): void;
}
