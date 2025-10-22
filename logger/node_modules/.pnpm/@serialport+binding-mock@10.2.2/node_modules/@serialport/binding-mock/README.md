# @serialport/binding-mock

```ts
import { MockBinding } from '@serialport/binding-mock'
const MockBinding = new MockBinding()

MockBinding.createPort('/dev/fakePort', { echo: true })
await MockBinding.write(Buffer.from('data')))
```
