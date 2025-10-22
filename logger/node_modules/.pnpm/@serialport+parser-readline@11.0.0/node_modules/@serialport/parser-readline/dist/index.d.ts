/// <reference types="node" />
/// <reference types="node" />
import { DelimiterParser } from '@serialport/parser-delimiter';
import { TransformOptions } from 'stream';
export interface ReadlineOptions extends TransformOptions {
    /** delimiter to use defaults to \n */
    delimiter?: string | Buffer | number[];
    /** include the delimiter at the end of the packet defaults to false */
    includeDelimiter?: boolean;
    /** Defaults to utf8 */
    encoding?: BufferEncoding;
}
/**
 *  A transform stream that emits data after a newline delimiter is received.
 * @summary To use the `Readline` parser, provide a delimiter (defaults to `\n`). Data is emitted as string controllable by the `encoding` option (defaults to `utf8`).
 */
export declare class ReadlineParser extends DelimiterParser {
    constructor(options?: ReadlineOptions);
}
