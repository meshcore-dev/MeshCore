/// <reference types="node" />
/// <reference types="node" />
import { Transform, TransformCallback, TransformOptions } from 'stream';
export interface DelimiterOptions extends TransformOptions {
    /** The delimiter on which to split incoming data. */
    delimiter: string | Buffer | number[];
    /** Should the delimiter be included at the end of data. Defaults to `false` */
    includeDelimiter?: boolean;
}
/**
 * A transform stream that emits data each time a byte sequence is received.
 * @extends Transform
 *
 * To use the `Delimiter` parser, provide a delimiter as a string, buffer, or array of bytes. Runs in O(n) time.
 */
export declare class DelimiterParser extends Transform {
    includeDelimiter: boolean;
    delimiter: Buffer;
    buffer: Buffer;
    constructor({ delimiter, includeDelimiter, ...options }: DelimiterOptions);
    _transform(chunk: Buffer, encoding: BufferEncoding, cb: TransformCallback): void;
    _flush(cb: TransformCallback): void;
}
