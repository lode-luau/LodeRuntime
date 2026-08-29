# Win32 FFI homologation

Run `ffi-win32-window.luau` manually on Windows x64 with the matching Debug or
Release `lode.exe` and native modules. This test is intentionally outside
sanity because it creates a real `user32` window and dispatches system
messages.

The script uses only the final APIs: `FFI.CDef`, `FFI.TypeOf`/layout queries,
`FFI.Load`, `FFI.new`, `FFI.Callback`, `Address.Of`/`Address.WBuffer`, and
`Memory.new`. It registers a named `WNDPROC` typedef and a typed
`WNDCLASSEXW`, then exercises `RegisterClassExW`, `CreateWindowExW`,
`ShowWindow`, `PeekMessageW`, `TranslateMessage`, `DispatchMessageW`,
`BeginPaint`/`EndPaint`, `DefWindowProcW`, `WM_DESTROY`/`PostQuitMessage`,
`DestroyWindow`, and `UnregisterClassW`.

Verify that:

- `WNDCLASSEXW` has the expected x64 size and `WNDPROC` field offset;
- `DefWindowProcW` executes synchronously inside the callback without a yield
  error, and each `WM_PAINT` balances `BeginPaint` with `EndPaint`;
- hit testing, caption, borders, keyboard/mouse delivery, minimizing,
  restoring, maximizing, and resizing remain handled by `DefWindowProcW`;
- drawing follows `WM_SIZE`, the window closes through the X, and the message
  queue is drained before `UnregisterClassW` and `Callback.Close()`;
- callback faults do not repeat indefinitely and no callback is invoked after
  `Close()`.

Pointers are opaque and non-owning. Keep UTF-16 buffers, typed values,
callbacks, and memory blocks alive for as long as Win32 retains their address;
`Memory.Resize`/`Memory.Free` invalidates associated leases. External C memory
has no verifiable bounds checking and native access violations remain outside
the Luau `pcall` contract.
