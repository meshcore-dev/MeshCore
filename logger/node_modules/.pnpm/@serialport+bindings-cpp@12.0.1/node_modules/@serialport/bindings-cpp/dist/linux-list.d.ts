/// <reference types="node" />
import { spawn } from 'child_process';
import { PortInfo } from '@serialport/bindings-interface';
export declare function linuxList(spawnCmd?: typeof spawn): Promise<PortInfo[]>;
