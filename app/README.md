# MDS over HID Application

Zephyr RTOS application implementing the Memfault Diagnostic Service (MDS) protocol over USB HID for nRF52840.

## Features

- USB HID interface for MDS protocol communication
- Memfault diagnostics data collection and streaming
- Configurable heartbeat metrics (10s for testing, 1h default for production)
- Support for all MDS protocol reports:
  - Supported Features (0x01)
  - Device Identifier (0x02)
  - Data URI (0x03)
  - Authorization (0x04)
  - Stream Control (0x05)
  - Stream Data (0x06)

## Hardware

- **Board**: nRF52840 DK (nrf52840dk/nrf52840)
- **USB**: VID 0x2fe3 (Zephyr Project), PID 0x0007

## Setup

### 1. Install Dependencies

```bash
# Zephyr SDK and west should already be installed
# Activate the Python virtual environment
source ../.venv/bin/activate
```

### 2. Configure Memfault

Copy the template configuration and add your Memfault project key:

```bash
cp prj.conf.template prj.conf
# Edit prj.conf and replace YOUR_PROJECT_KEY_HERE with your actual key
```

Get your project key from https://app.memfault.com/ → Settings → General

### 3. Build

```bash
west build -b nrf52840dk/nrf52840
```

### 4. Flash

```bash
west flash
```

## Configuration

Key configuration options in `prj.conf`:

- `CONFIG_MEMFAULT_NCS_PROJECT_KEY`: Your Memfault project key (required)
- `CONFIG_MEMFAULT_NCS_DEVICE_ID`: Unique device identifier
- `CONFIG_MEMFAULT_METRICS_HEARTBEAT_INTERVAL_SECS`: Metrics collection interval (10s for testing)

## Protocol

This device implements the MDS protocol over USB HID. The host can:
1. Query device information via Feature Reports (IDs 1-4)
2. Enable/disable streaming via Stream Control (ID 5)
3. Receive diagnostic data chunks via Stream Data (ID 6)

## Development

The application uses Memfault SDK integration for NCS. Diagnostic data is automatically collected and queued for transmission when streaming is enabled by the host.
