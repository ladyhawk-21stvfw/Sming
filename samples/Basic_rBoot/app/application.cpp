#include <SmingCore.h>
#include <Network/RbootHttpUpdater.h>

// download urls, set appropriately
#define ROM_0_URL "http://192.168.7.5:80/rom0.bin"
#define ROM_1_URL "http://192.168.7.5:80/rom1.bin"
#define SPIFFS_URL "http://192.168.7.5:80/spiff_rom.bin"

// If you want, you can define WiFi settings globally in Eclipse Environment Variables
#ifndef WIFI_SSID
#define WIFI_SSID "PleaseEnterSSID" // Put you SSID and Password here
#define WIFI_PWD "PleaseEnterPass"
#endif

RbootHttpUpdater* otaUpdater;
Storage::Partition spiffsPartition;

Storage::Partition findSpiffsPartition(uint8_t slot)
{
	String name = F("spiffs");
	name += slot;
	auto part = Storage::findPartition(name);
	if(!part) {
		debug_w("Partition '%s' not found", name.c_str());
	}
	return part;
}

void otaUpdateCallBack(RbootHttpUpdater& client, bool result)
{
	Serial.println("In callback...");
	if(result == true) {
		// success
		uint8 slot;
		slot = rboot_get_current_rom();
		if(slot == 0) {
			slot = 1;
		} else {
			slot = 0;
		}
		// set to boot new rom and then reboot
		Serial.printf("Firmware updated, rebooting to rom %d...\r\n", slot);
		rboot_set_current_rom(slot);
		System.restart();
	} else {
		// fail
		Serial.println("Firmware update failed!");
	}
}

void OtaUpdate()
{
	uint8 slot;
	rboot_config bootconf;

	Serial.println("Updating...");

	// need a clean object, otherwise if run before and failed will not run again
	if(otaUpdater) {
		delete otaUpdater;
	}
	otaUpdater = new RbootHttpUpdater();

	// select rom slot to flash
	bootconf = rboot_get_config();
	slot = bootconf.current_rom;
	if(slot == 0) {
		slot = 1;
	} else {
		slot = 0;
	}

#ifndef RBOOT_TWO_ROMS
	// flash rom to position indicated in the rBoot config rom table
	otaUpdater->addItem(bootconf.roms[slot], ROM_0_URL);
#else
	// flash appropriate ROM
	otaUpdater->addItem(bootconf.roms[slot], (slot == 0) ? ROM_0_URL : ROM_1_URL);
#endif

	auto part = findSpiffsPartition(slot);
	if(part) {
		// use user supplied values (defaults for 4mb flash in hardware config)
		otaUpdater->addItem(part.address(), SPIFFS_URL, part.size());
	}

	// request switch and reboot on success
	//otaUpdater->switchToRom(slot);
	// and/or set a callback (called on failure or success without switching requested)
	otaUpdater->setCallback(otaUpdateCallBack);

	// start update
	otaUpdater->start();
}

void Switch()
{
	uint8 before, after;
	before = rboot_get_current_rom();
	if(before == 0) {
		after = 1;
	} else {
		after = 0;
	}
	Serial.printf("Swapping from rom %d to rom %d.\r\n", before, after);
	rboot_set_current_rom(after);
	Serial.println("Restarting...\r\n");
	System.restart();
}

void ShowInfo()
{
	Serial.printf("\r\nSDK: v%s\r\n", system_get_sdk_version());
	Serial.printf("Free Heap: %d\r\n", system_get_free_heap_size());
	Serial.printf("CPU Frequency: %d MHz\r\n", system_get_cpu_freq());
	Serial.printf("System Chip ID: %x\r\n", system_get_chip_id());
	Serial.printf("SPI Flash ID: %x\r\n", spi_flash_get_id());
	//Serial.printf("SPI Flash Size: %d\r\n", (1 << ((spi_flash_get_id() >> 16) & 0xff)));

	rboot_config conf;
	conf = rboot_get_config();

	debugf("Count: %d", conf.count);
	debugf("ROM 0: %d", conf.roms[0]);
	debugf("ROM 1: %d", conf.roms[1]);
	debugf("ROM 2: %d", conf.roms[2]);
	debugf("GPIO ROM: %d", conf.gpio_rom);
}

void serialCallBack(Stream& stream, char arrivedChar, unsigned short availableCharsCount)
{
	int pos = stream.indexOf('\n');
	if(pos > -1) {
		char str[pos + 1];
		for(int i = 0; i < pos + 1; i++) {
			str[i] = stream.read();
			if(str[i] == '\r' || str[i] == '\n') {
				str[i] = '\0';
			}
		}

		if(!strcmp(str, "connect")) {
			// connect to wifi
			WifiStation.config(WIFI_SSID, WIFI_PWD);
			WifiStation.enable(true);
			WifiStation.connect();
		} else if(!strcmp(str, "ip")) {
			Serial.print("ip: ");
			Serial.print(WifiStation.getIP());
			Serial.print("mac: ");
			Serial.println(WifiStation.getMacAddress());
		} else if(!strcmp(str, "ota")) {
			OtaUpdate();
		} else if(!strcmp(str, "switch")) {
			Switch();
		} else if(!strcmp(str, "restart")) {
			System.restart();
		} else if(!strcmp(str, "ls")) {
			Vector<String> files = fileList();
			Serial.printf("filecount %d\r\n", files.count());
			for(unsigned int i = 0; i < files.count(); i++) {
				Serial.println(files[i]);
			}
		} else if(!strcmp(str, "cat")) {
			Vector<String> files = fileList();
			if(files.count() > 0) {
				Serial.printf("dumping file %s:\r\n", files[0].c_str());
				Serial.println(fileGetContent(files[0]));
			} else {
				Serial.println("Empty spiffs!");
			}
		} else if(!strcmp(str, "info")) {
			ShowInfo();
		} else if(!strcmp(str, "help")) {
			Serial.println();
			Serial.println("available commands:");
			Serial.println("  help - display this message");
			Serial.println("  ip - show current ip address");
			Serial.println("  connect - connect to wifi");
			Serial.println("  restart - restart the esp8266");
			Serial.println("  switch - switch to the other rom and reboot");
			Serial.println("  ota - perform ota update, switch rom and reboot");
			Serial.println("  info - show esp8266 info");
			if(spiffsPartition) {
				Serial.println("  ls - list files in spiffs");
				Serial.println("  cat - show first file in spiffs");
			}
			Serial.println();
		} else {
			Serial.println("unknown command");
		}
	}
}

void init()
{
	Serial.begin(SERIAL_BAUD_RATE); // 115200 by default
	Serial.systemDebugOutput(true); // Debug output to serial

	// mount spiffs
	auto slot = rboot_get_current_rom();
	spiffsPartition = findSpiffsPartition(slot);
	if(spiffsPartition) {
		debugf("trying to mount '%s' at 0x%08x, length %d", spiffsPartition.name().c_str(), spiffsPartition.address(),
			   spiffsPartition.size());
		spiffs_mount(spiffsPartition);
	}

	WifiAccessPoint.enable(false);

	Serial.printf("\r\nCurrently running rom %d.\r\n", slot);
	Serial.println("Type 'help' and press enter for instructions.");
	Serial.println();

	Serial.onDataReceived(serialCallBack);
}
