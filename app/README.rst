.. zephyr:code-sample:: memfault-mds-hid
   :name: Memfault Diagnostic Service over HID
   :relevant-api: usbd_api usbd_hid_device

   Implement Memfault Diagnostic Service (MDS) protocol over USB HID.

Overview
********

This sample app demonstrates the implementation of the Memfault Diagnostic
Service (MDS) protocol over USB HID. It allows diagnostic data from an embedded
device to be streamed to a host computer via USB HID, where it can be uploaded
to the Memfault cloud.

The MDS protocol, originally designed for Bluetooth GATT, has been adapted to
work over HID using:

- **Feature Reports**: Provide device configuration (device ID, data URI,
  authorization)
- **Output Reports**: Control diagnostic data streaming (enable/disable)
- **Input Reports**: Deliver diagnostic chunk data to the host

The implementation is compatible with Memfault's packetizer API and can work
with or without the full Memfault SDK enabled.

Requirements
************

This project requires:

- USB device driver support
- An LED configured via the ``led0`` devicetree alias (used to indicate chunk
  transmission)
- (Optional) Memfault SDK for real diagnostic data collection

Protocol Overview
*****************

The MDS-over-HID protocol uses the following HID report types:

**Feature Reports (Host -> Device GET_REPORT)**

- Report ID 0x01: Supported Features (4 bytes)
- Report ID 0x02: Device Identifier (up to 64 bytes, string)
- Report ID 0x03: Data URI (up to 128 bytes, string)
- Report ID 0x04: Authorization Header (up to 128 bytes, string)

**Output Reports (Host -> Device SET_REPORT)**

- Report ID 0x05: Stream Control (1 byte: 0x00=disable, 0x01=enable)

**Input Reports (Device -> Host)**

- Report ID 0x06: Stream Data (64 bytes: 1 byte sequence + up to 63 bytes data)

The sequence number (bits 0-4) wraps at 31 and allows the host to detect
dropped or duplicate packets.

Building and Running
********************

Basic Build (without Memfault SDK)
===================================

This sample can be built for the nRF52840 DK:

.. code-block:: console

   west build -b nrf52840dk/nrf52840

The application will build with test data support. When streaming is enabled,
it will send "TEST_DATA" chunks to demonstrate the protocol.

Build with Memfault SDK
========================

To enable real Memfault diagnostics:

1. Edit ``prj.conf`` and uncomment the Memfault configuration:

   .. code-block:: none

      CONFIG_MEMFAULT=y
      CONFIG_MEMFAULT_NCS_PROJECT_KEY="your-project-key"
      CONFIG_MEMFAULT_HTTP_ENABLE=y

2. Implement the required Memfault platform APIs (device info, etc.)
   See: https://docs.memfault.com/docs/mcu/zephyr-guide/

3. Build and flash:

   .. code-block:: console

      west build -b nrf52840dk/nrf52840
      west flash

Using the Application
**********************

After flashing, connect the board to your host computer via USB. The device
will enumerate as a vendor-defined HID device.

On Linux, you can verify the device with:

.. code-block:: console

   dmesg | tail
   # Look for: "hid-generic ... input: USB HID v1.11 Device"

Protocol Workflow
=================

1. **Device Configuration**: The host reads feature reports to get:

   - Device identifier (serial number)
   - Data URI for chunk upload
   - Authorization header (Memfault project key)

2. **Enable Streaming**: The host sends an output report (0x05) with value
   0x01 to enable streaming

3. **Receive Chunks**: The device sends diagnostic chunks via input reports
   (0x06), each with a sequence number and up to 63 bytes of data

4. **Upload to Cloud**: The host uploads each chunk to the Memfault cloud using
   the URI and authorization from step 1

5. **Disable Streaming**: The host sends output report (0x05) with value 0x00
   to stop streaming

The LED will toggle with each transmitted chunk to provide visual feedback.

Host-Side Implementation
*************************

The ``inspiration/`` folder contains reference C code for a host-side client
that can:

- Read device configuration via feature reports
- Control streaming via output reports
- Receive and upload chunks to Memfault cloud

You can use this as a starting point for your own host-side gateway application.

Testing
*******

Without the full host-side implementation, you can test the HID reports using
standard HID tools:

On Linux with ``hidapi``:

.. code-block:: python

   import hid

   # Open the device
   dev = hid.device()
   dev.open(0x2fe3, 0x0007)  # Vendor ID, Product ID

   # Read device identifier (Feature Report 0x02)
   data = dev.get_feature_report(0x02, 64)
   print("Device ID:", bytes(data).decode('utf-8', errors='ignore'))

   # Enable streaming (Output Report 0x05)
   dev.write([0x05, 0x01])

   # Read stream data (Input Report 0x06)
   while True:
       data = dev.read(65, timeout_ms=1000)
       if data and data[0] == 0x06:
           seq = data[1] & 0x1F
           chunk = bytes(data[2:])
           print(f"Chunk {seq}: {len(chunk)} bytes")

Notes
*****

- The sequence number is stored in bits 0-4 of the first data byte, allowing
  values 0-31 before wrapping
- Chunk size is limited to 63 bytes per packet due to HID input report size
- The implementation follows the same patterns as the Bluetooth MDS service for
  consistency across transport layers
