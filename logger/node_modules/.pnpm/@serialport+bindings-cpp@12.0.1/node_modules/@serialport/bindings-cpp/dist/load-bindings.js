"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.asyncWrite = exports.asyncRead = exports.asyncUpdate = exports.asyncSet = exports.asyncOpen = exports.asyncList = exports.asyncGetBaudRate = exports.asyncGet = exports.asyncFlush = exports.asyncDrain = exports.asyncClose = void 0;
const node_gyp_build_1 = __importDefault(require("node-gyp-build"));
const util_1 = require("util");
const path_1 = require("path");
// eslint-disable-next-line @typescript-eslint/no-explicit-any
const binding = (0, node_gyp_build_1.default)((0, path_1.join)(__dirname, '../'));
exports.asyncClose = binding.close ? (0, util_1.promisify)(binding.close) : async () => { throw new Error('"binding.close" Method not implemented'); };
exports.asyncDrain = binding.drain ? (0, util_1.promisify)(binding.drain) : async () => { throw new Error('"binding.drain" Method not implemented'); };
exports.asyncFlush = binding.flush ? (0, util_1.promisify)(binding.flush) : async () => { throw new Error('"binding.flush" Method not implemented'); };
exports.asyncGet = binding.get ? (0, util_1.promisify)(binding.get) : async () => { throw new Error('"binding.get" Method not implemented'); };
exports.asyncGetBaudRate = binding.getBaudRate ? (0, util_1.promisify)(binding.getBaudRate) : async () => { throw new Error('"binding.getBaudRate" Method not implemented'); };
exports.asyncList = binding.list ? (0, util_1.promisify)(binding.list) : async () => { throw new Error('"binding.list" Method not implemented'); };
exports.asyncOpen = binding.open ? (0, util_1.promisify)(binding.open) : async () => { throw new Error('"binding.open" Method not implemented'); };
exports.asyncSet = binding.set ? (0, util_1.promisify)(binding.set) : async () => { throw new Error('"binding.set" Method not implemented'); };
exports.asyncUpdate = binding.update ? (0, util_1.promisify)(binding.update) : async () => { throw new Error('"binding.update" Method not implemented'); };
exports.asyncRead = binding.read ? (0, util_1.promisify)(binding.read) : async () => { throw new Error('"binding.read" Method not implemented'); };
exports.asyncWrite = binding.write ? (0, util_1.promisify)(binding.write) : async () => { throw new Error('"binding.write" Method not implemented'); };
