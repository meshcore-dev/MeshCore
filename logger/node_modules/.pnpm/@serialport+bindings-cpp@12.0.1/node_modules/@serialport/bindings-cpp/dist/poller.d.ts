/// <reference types="node" />
import { EventEmitter } from 'events';
interface PollerClass {
    new (fd: number, cb: (err: Error, flag: number) => void): PollerInstance;
}
interface PollerInstance {
    poll(flag: number): void;
    stop(): void;
    destroy(): void;
}
export declare const EVENTS: {
    UV_READABLE: number;
    UV_WRITABLE: number;
    UV_DISCONNECT: number;
};
/**
 * Polls unix systems for readable or writable states of a file or serialport
 */
export declare class Poller extends EventEmitter {
    poller: PollerInstance;
    constructor(fd: number, FDPoller?: PollerClass);
    /**
     * Wait for the next event to occur
     * @param {string} event ('readable'|'writable'|'disconnect')
     * @returns {Poller} returns itself
     */
    once(event: 'readable' | 'writable' | 'disconnect', callback: (err: null | Error) => void): this;
    /**
     * Ask the bindings to listen for an event, it is recommend to use `.once()` for easy use
     * @param {EVENTS} eventFlag polls for an event or group of events based upon a flag.
     */
    poll(eventFlag?: number): void;
    /**
     * Stop listening for events and cancel all outstanding listening with an error
     */
    stop(): void;
    destroy(): void;
    emitCanceled(): void;
}
export {};
