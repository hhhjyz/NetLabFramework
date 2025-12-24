# Lab8 Go Web Server

Run from repo root or from `lab8/`.

## Build

```bash
go build -o lab8_server ./lab8
```

## Run

```bash
./lab8_server -port 6052 -mode parse
./lab8_server -port 6053 -mode echo
./lab8_server -port 6054 -mode map
./lab8_server -port 6055 -mode full
```

Options:
- `-host`: listen host, default `0.0.0.0`
- `-port`: listen port, default `8080`
- `-mode`: `parse|echo|map|full`
- `-root`: assets root (default tries `assets` then `../assets`)
- `-user` / `-pass`: login credentials for `/dopost`

## Test

Tests run in CI on pushes. To run the same workflow locally, use `act` from the repo root:

```bash
act push
```
