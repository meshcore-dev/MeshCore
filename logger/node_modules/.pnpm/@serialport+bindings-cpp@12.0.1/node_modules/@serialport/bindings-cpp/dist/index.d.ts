import { DarwinBindingInterface } from './darwin';
import { LinuxBindingInterface } from './linux';
import { WindowsBindingInterface } from './win32';
export * from '@serialport/bindings-interface';
export * from './darwin';
export * from './linux';
export * from './win32';
export * from './errors';
export type AutoDetectTypes = DarwinBindingInterface | WindowsBindingInterface | LinuxBindingInterface;
/**
 * This is an auto detected binding for your current platform
 */
export declare function autoDetect(): AutoDetectTypes;
