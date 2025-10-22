/// <reference types="node" />
/// <reference types="node" />
import { Transform, TransformCallback, TransformOptions } from 'stream';
export interface RegexParserOptions extends TransformOptions {
    /** The regular expression to use to split incoming text */
    regex: RegExp | string | Buffer;
    /** Defaults to utf8 */
    encoding?: BufferEncoding;
}
/**
 * A transform stream that uses a regular expression to split the incoming text upon.
 *
 * To use the `Regex` parser provide a regular expression to split the incoming text upon. Data is emitted as string controllable by the `encoding` option (defaults to `utf8`).
 */
export declare class RegexParser extends Transform {
    regex: RegExp;
    data: string;
    constructor({ regex, ...options }: RegexParserOptions);
    _transform(chunk: string, encoding: BufferEncoding, cb: TransformCallback): void;
    _flush(cb: TransformCallback): void;
}
