package com.example.hc_05_application

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Divider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.example.hc_05_application.bluetooth.BtSerialClient
import com.example.hc_05_application.bluetooth.ClassicDiscovery
import com.example.hc_05_application.ui.theme.HC05ApplicationTheme
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

data class FoundDevice(
    val name: String,
    val address: String,
    val device: BluetoothDevice
)

@OptIn(ExperimentalMaterial3Api::class)
class MainActivity : ComponentActivity() {

    private var discovery: ClassicDiscovery? = null
    private var btClient: BtSerialClient? = null
    private var bluetoothAdapter: BluetoothAdapter? = null

    private val foundDevices = mutableStateListOf<FoundDevice>()

    private var isScanning by mutableStateOf(false)
    private var isConnected by mutableStateOf(false)
    private var connectedDeviceName by mutableStateOf("None")
    private var statusMessage by mutableStateOf("Ready")
    private var redOn by mutableStateOf(false)
    private var greenOn by mutableStateOf(false)
    private var blueOn by mutableStateOf(false)

    private val enableBluetoothLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
            statusMessage = if (bluetoothAdapter?.isEnabled == true) {
                "Bluetooth enabled"
            } else {
                "Bluetooth must be enabled to continue"
            }
        }

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { perms ->
            val allGranted = perms.values.all { it }
            if (allGranted) {
                statusMessage = "Bluetooth permissions granted"
            } else {
                statusMessage = "Bluetooth permissions denied"
                toast("Bluetooth permissions are required for scanning and connecting.")
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        val bluetoothManager = getSystemService(BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothAdapter = bluetoothManager.adapter

        btClient = bluetoothAdapter?.let { BtSerialClient(it) }

        setContent {
            HC05ApplicationTheme {
                Scaffold(
                    topBar = {
                        TopAppBar(
                            title = { Text("HC-05 Controller") }
                        )
                    }
                ) { innerPadding ->
                    MainScreen(
                        modifier = Modifier
                            .padding(innerPadding)
                            .fillMaxSize(),
                        isScanning = isScanning,
                        isConnected = isConnected,
                        connectedDeviceName = connectedDeviceName,
                        statusMessage = statusMessage,
                        foundDevices = foundDevices,
                        redOn = redOn,
                        greenOn = greenOn,
                        blueOn = blueOn,
                        onRequestPermissions = { requestBluetoothPermissions() },
                        onEnableBluetooth = { ensureBluetoothEnabled() },
                        onScan = { startDiscovery() },
                        onStopScan = { stopDiscovery() },
                        onConnect = { device -> connectToDevice(device) },
                        onDisconnect = { disconnectDevice() },
                        onHelp = { sendCommand("HELP") },
                        onState = { sendCommand("STATE") },
                        onExit = { sendCommand("EXIT") },
                        onAllOff = {
                            redOn = false
                            greenOn = false
                            blueOn = false
                            sendCommand("X")
                        },
                        onSetRed = { on ->
                            redOn = on
                            sendCommand(if (on) "R1" else "R0")
                        },
                        onSetGreen = { on ->
                            greenOn = on
                            sendCommand(if (on) "G1" else "G0")
                        },
                        onSetBlue = { on ->
                            blueOn = on
                            sendCommand(if (on) "B1" else "B0")
                        },
                        onSendRgb = { r, g, b ->
                            redOn = r
                            greenOn = g
                            blueOn = b
                            sendCommand("RGB:${if (r) 1 else 0}${if (g) 1 else 0}${if (b) 1 else 0}")
                        }
                    )
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        stopDiscovery()
        CoroutineScope(Dispatchers.IO).launch {
            btClient?.disconnect()
        }
    }

    private fun requestBluetoothPermissions() {
        val permissions = mutableListOf<String>()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions += Manifest.permission.BLUETOOTH_SCAN
            permissions += Manifest.permission.BLUETOOTH_CONNECT
        } else {
            permissions += Manifest.permission.ACCESS_FINE_LOCATION
        }

        permissionLauncher.launch(permissions.toTypedArray())
    }

    private fun ensureBluetoothEnabled() {
        val adapter = bluetoothAdapter
        if (adapter == null) {
            statusMessage = "This device does not support Bluetooth"
            return
        }

        if (!adapter.isEnabled) {
            val intent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
            enableBluetoothLauncher.launch(intent)
        } else {
            statusMessage = "Bluetooth already enabled"
        }
    }

    @SuppressLint("MissingPermission")
    private fun startDiscovery() {
        val adapter = bluetoothAdapter
        if (adapter == null) {
            statusMessage = "Bluetooth not supported"
            return
        }

        if (!adapter.isEnabled) {
            statusMessage = "Enable Bluetooth first"
            toast("Enable Bluetooth first")
            return
        }

        foundDevices.clear()
        stopDiscovery()

        discovery = ClassicDiscovery(
            context = this,
            adapter = adapter,
            onFound = { device ->
                val name = try {
                    device.name ?: "Unknown Device"
                } catch (_: SecurityException) {
                    "Unknown Device"
                }

                val alreadyExists = foundDevices.any { it.address == device.address }
                if (!alreadyExists) {
                    foundDevices.add(
                        FoundDevice(
                            name = name,
                            address = device.address,
                            device = device
                        )
                    )
                }
            },
            onFinished = {
                isScanning = false
                statusMessage = "Discovery finished"
            }
        )

        isScanning = true
        statusMessage = "Scanning..."
        discovery?.start()
    }

    private fun stopDiscovery() {
        try {
            discovery?.stop()
        } catch (e: SecurityException) {
            // Log the error or handle the lack of permission gracefully
            Log.e("Discovery", "Permission lost before discovery could be stopped", e)
        }

        discovery = null
        isScanning = false
    }

    @SuppressLint("MissingPermission")
    private fun connectToDevice(device: BluetoothDevice) {
        val client = btClient ?: return

        statusMessage = "Connecting to ${device.name ?: device.address}..."

        CoroutineScope(Dispatchers.IO).launch {
            try {
                stopDiscovery()
                client.connect(device)

                runOnUiThread {
                    isConnected = true
                    connectedDeviceName = device.name ?: device.address
                    statusMessage = "Connected to ${device.name ?: device.address}"
                    toast("Connected")
                }
            } catch (e: Exception) {
                runOnUiThread {
                    isConnected = false
                    connectedDeviceName = "None"
                    statusMessage = "Connection failed: ${e.message}"
                    toast("Connection failed")
                }
            }
        }
    }

    private fun disconnectDevice() {
        val client = btClient ?: return

        CoroutineScope(Dispatchers.IO).launch {
            try {
                client.disconnect()
            } catch (_: Exception) {
            }

            runOnUiThread {
                isConnected = false
                connectedDeviceName = "None"
                statusMessage = "Disconnected"
                toast("Disconnected")
            }
        }
    }

    private fun sendCommand(cmd: String) {
        val client = btClient ?: return

        if (!isConnected) {
            toast("Not connected")
            return
        }

        CoroutineScope(Dispatchers.IO).launch {
            try {
                client.sendLine(cmd)
                runOnUiThread {
                    statusMessage = "Sent: $cmd"
                }
            } catch (e: Exception) {
                runOnUiThread {
                    statusMessage = "Send failed: ${e.message}"
                    toast("Send failed")
                }
            }
        }
    }

    private fun toast(msg: String) {
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
    }
}

@Composable
fun MainScreen(
    modifier: Modifier = Modifier,
    isScanning: Boolean,
    isConnected: Boolean,
    connectedDeviceName: String,
    statusMessage: String,
    foundDevices: List<FoundDevice>,
    redOn: Boolean,
    greenOn: Boolean,
    blueOn: Boolean,
    onRequestPermissions: () -> Unit,
    onEnableBluetooth: () -> Unit,
    onScan: () -> Unit,
    onStopScan: () -> Unit,
    onConnect: (BluetoothDevice) -> Unit,
    onDisconnect: () -> Unit,
    onHelp: () -> Unit,
    onState: () -> Unit,
    onExit: () -> Unit,
    onAllOff: () -> Unit,
    onSetRed: (Boolean) -> Unit,
    onSetGreen: (Boolean) -> Unit,
    onSetBlue: (Boolean) -> Unit,
    onSendRgb: (Boolean, Boolean, Boolean) -> Unit
) {
    Column(
        modifier = modifier.padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("Status: $statusMessage", style = MaterialTheme.typography.bodyLarge)
                Spacer(modifier = Modifier.height(4.dp))
                Text("Connected device: $connectedDeviceName")
                Spacer(modifier = Modifier.height(8.dp))

                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onRequestPermissions) {
                        Text("Grant Permissions")
                    }
                    Button(onClick = onEnableBluetooth) {
                        Text("Enable Bluetooth")
                    }
                }

                Spacer(modifier = Modifier.height(8.dp))

                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onScan, enabled = !isScanning) {
                        Text("Scan")
                    }
                    Button(onClick = onStopScan, enabled = isScanning) {
                        Text("Stop Scan")
                    }
                    Button(onClick = onDisconnect, enabled = isConnected) {
                        Text("Disconnect")
                    }
                }
            }
        }

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("Discovered Devices", style = MaterialTheme.typography.titleMedium)
                Spacer(modifier = Modifier.height(8.dp))

                if (foundDevices.isEmpty()) {
                    Text("No devices found yet.")
                } else {
                    LazyColumn(modifier = Modifier.height(180.dp)) {
                        items(foundDevices) { found ->
                            DeviceRow(
                                deviceName = found.name,
                                address = found.address,
                                onConnect = { onConnect(found.device) }
                            )
                            Divider()
                        }
                    }
                }
            }
        }

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("Controls", style = MaterialTheme.typography.titleMedium)

                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                        Text("Red")
                        Spacer(modifier = Modifier.padding(4.dp))
                        Switch(
                            checked = redOn,
                            onCheckedChange = onSetRed,
                            enabled = isConnected
                        )
                    }

                    Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                        Text("Green")
                        Spacer(modifier = Modifier.padding(4.dp))
                        Switch(
                            checked = greenOn,
                            onCheckedChange = onSetGreen,
                            enabled = isConnected
                        )
                    }

                    Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                        Text("Blue")
                        Spacer(modifier = Modifier.padding(4.dp))
                        Switch(
                            checked = blueOn,
                            onCheckedChange = onSetBlue,
                            enabled = isConnected
                        )
                    }
                }

                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onAllOff, enabled = isConnected) {
                        Text("All Off")
                    }
                    Button(
                        onClick = { onSendRgb(true, true, true) },
                        enabled = isConnected
                    ) {
                        Text("White")
                    }
                    Button(
                        onClick = { onSendRgb(true, false, true) },
                        enabled = isConnected
                    ) {
                        Text("Magenta")
                    }
                }

                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onHelp, enabled = isConnected) {
                        Text("HELP")
                    }
                    Button(onClick = onState, enabled = isConnected) {
                        Text("STATE")
                    }
                    Button(onClick = onExit, enabled = isConnected) {
                        Text("EXIT")
                    }
                }
            }
        }
    }
}

@Composable
fun DeviceRow(
    deviceName: String,
    address: String,
    onConnect: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 8.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Column {
            Text(deviceName, style = MaterialTheme.typography.bodyLarge)
            Text(address, style = MaterialTheme.typography.bodySmall)
        }

        TextButton(onClick = onConnect) {
            Text("Connect")
        }
    }
}