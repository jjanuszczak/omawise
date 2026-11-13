# Reader UX prototype

This is a disposable, mock-data Qt Quick prototype for Phase 0. It has no network client, Readwise token, or persistent storage. All displayed documents and document text are fictional; prototype actions do not sync.

## Build and run

```sh
mkdir -p build
cd build
qmake6 ../reader-ux.pro
make -j"$(nproc)"
./reader-ux
```

## Prototype flow

- `/` focuses library search.
- `j` and `k` move through the list.
- `Enter` opens the selected document; `Escape` returns to the library.
- `Ctrl+K` opens the command palette.
- `?` shows the keyboard reference.
- `a` simulates archiving the selected item (in memory only).

This is for layout and interaction exploration, not feature parity or performance measurement.
