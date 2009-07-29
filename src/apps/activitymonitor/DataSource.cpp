/*
 * Copyright 2008-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "DataSource.h"

#include <stdio.h>
#include <stdint.h>

#include <OS.h>
#include <String.h>

#include "SystemInfo.h"


const DataSource* kSources[] = {
	new UsedMemoryDataSource(),
	new CachedMemoryDataSource(),
	new SwapSpaceDataSource(),
	new PageFaultsDataSource(),
	new CPUUsageDataSource(),
	new CPUCombinedUsageDataSource(),
	new NetworkUsageDataSource(true),
	new NetworkUsageDataSource(false),
	new BlockCacheDataSource(),
	new SemaphoresDataSource(),
	new PortsDataSource(),
	new ThreadsDataSource(),
	new TeamsDataSource(),
	new RunningAppsDataSource(),
	new ClipboardSizeDataSource(false),
	new ClipboardSizeDataSource(true),
	new MediaNodesDataSource()
};
const size_t kSourcesCount = sizeof(kSources) / sizeof(kSources[0]);


DataSource::DataSource(int64 initialMin, int64 initialMax)
	:
	fMinimum(initialMin),
	fMaximum(initialMax),
	fInterval(1000000LL),
	fColor((rgb_color){200, 0, 0})
{
}


DataSource::DataSource()
	:
	fMinimum(0),
	fMaximum(100),
	fInterval(1000000LL),
	fColor((rgb_color){200, 0, 0})
{
}


DataSource::DataSource(const DataSource& other)
{
	fMinimum = other.fMinimum;
	fMaximum = other.fMaximum;
	fInterval = other.fInterval;
	fColor = other.fColor;
}


DataSource::~DataSource()
{
}


DataSource*
DataSource::Copy() const
{
	return NULL;
		// this class cannot be copied
}


DataSource*
DataSource::CopyForCPU(int32 cpu) const
{
	return Copy();
}


int64
DataSource::Minimum() const
{
	return fMinimum;
}


int64
DataSource::Maximum() const
{
	return fMaximum;
}


bigtime_t
DataSource::RefreshInterval() const
{
	return fInterval;
}


void
DataSource::SetLimits(int64 min, int64 max)
{
	fMinimum = min;
	fMaximum = max;
}


void
DataSource::SetRefreshInterval(bigtime_t interval)
{
	fInterval = interval;
}


void
DataSource::SetColor(rgb_color color)
{
	fColor = color;
}


int64
DataSource::NextValue(SystemInfo& info)
{
	return 0;
}


void
DataSource::Print(BString& text, int64 value) const
{
	text = "";
	text << value;
}


const char*
DataSource::Name() const
{
	return Label();
}


const char*
DataSource::ShortLabel() const
{
	return Label();
}


const char*
DataSource::Label() const
{
	return "";
}


const char*
DataSource::Unit() const
{
	return "";
}


rgb_color
DataSource::Color() const
{
	return fColor;
}


bool
DataSource::AdaptiveScale() const
{
	return false;
}


scale_type
DataSource::ScaleType() const
{
	return kNoScale;
}


int32
DataSource::CPU() const
{
	return 0;
}


bool
DataSource::PerCPU() const
{
	return false;
}


bool
DataSource::MultiCPUOnly() const
{
	return false;
}


bool
DataSource::Primary() const
{
	return false;
}


/*static*/ int32
DataSource::CountSources()
{
	return kSourcesCount;
}


/*static*/ const DataSource*
DataSource::SourceAt(int32 index)
{
	if (index >= (int32)kSourcesCount || index < 0)
		return NULL;

	return kSources[index];
}


/*static*/ const DataSource*
DataSource::FindSource(const char* name)
{
	for (uint32 i = 0; i < kSourcesCount; i++) {
		const DataSource* source = kSources[i];
		if (!strcmp(source->Name(), name))
			return source;
	}

	return NULL;
}


/*static*/ int32
DataSource::IndexOf(const DataSource* source)
{
	const char* name = source->Name();

	for (uint32 i = 0; i < kSourcesCount; i++) {
		if (!strcmp(kSources[i]->Name(), name))
			return i;
	}

	return -1;
}


//	#pragma mark -


MemoryDataSource::MemoryDataSource()
{
	SystemInfo info;

	fMinimum = 0;
	fMaximum = info.MaxMemory();
}


MemoryDataSource::~MemoryDataSource()
{
}


void
MemoryDataSource::Print(BString& text, int64 value) const
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%.1f MB", value / 1048576.0);

	text = buffer;
}


const char*
MemoryDataSource::Unit() const
{
	return "MB";
}


//	#pragma mark -


UsedMemoryDataSource::UsedMemoryDataSource()
{
}


UsedMemoryDataSource::~UsedMemoryDataSource()
{
}


DataSource*
UsedMemoryDataSource::Copy() const
{
	return new UsedMemoryDataSource(*this);
}


int64
UsedMemoryDataSource::NextValue(SystemInfo& info)
{
	return info.UsedMemory();
}


const char*
UsedMemoryDataSource::Label() const
{
	return "Used Memory";
}


const char*
UsedMemoryDataSource::ShortLabel() const
{
	return "Memory";
}


bool
UsedMemoryDataSource::Primary() const
{
	return true;
}


//	#pragma mark -


CachedMemoryDataSource::CachedMemoryDataSource()
{
	fColor = (rgb_color){0, 200, 0};
}


CachedMemoryDataSource::~CachedMemoryDataSource()
{
}


DataSource*
CachedMemoryDataSource::Copy() const
{
	return new CachedMemoryDataSource(*this);
}


int64
CachedMemoryDataSource::NextValue(SystemInfo& info)
{
	return info.CachedMemory();
}


const char*
CachedMemoryDataSource::Label() const
{
	return "Cached Memory";
}


const char*
CachedMemoryDataSource::ShortLabel() const
{
	return "Cache";
}


bool
CachedMemoryDataSource::Primary() const
{
	return true;
}


//	#pragma mark -


SwapSpaceDataSource::SwapSpaceDataSource()
{
	SystemInfo info;

	fColor = (rgb_color){0, 120, 0};
	fMaximum = info.MaxSwapSpace();
}


SwapSpaceDataSource::~SwapSpaceDataSource()
{
}


DataSource*
SwapSpaceDataSource::Copy() const
{
	return new SwapSpaceDataSource(*this);
}


int64
SwapSpaceDataSource::NextValue(SystemInfo& info)
{
	return info.UsedSwapSpace();
}


const char*
SwapSpaceDataSource::Label() const
{
	return "Swap Space";
}


const char*
SwapSpaceDataSource::ShortLabel() const
{
	return "Swap";
}


bool
SwapSpaceDataSource::Primary() const
{
	return true;
}


//	#pragma mark -


BlockCacheDataSource::BlockCacheDataSource()
{
	fColor = (rgb_color){0, 120, 0};
}


BlockCacheDataSource::~BlockCacheDataSource()
{
}


DataSource*
BlockCacheDataSource::Copy() const
{
	return new BlockCacheDataSource(*this);
}


int64
BlockCacheDataSource::NextValue(SystemInfo& info)
{
	system_memory_info memoryInfo;
	status_t status = get_system_info_etc(B_MEMORY_INFO, &memoryInfo,
		sizeof(system_memory_info));
	if (status != B_OK)
		return 0;

	return memoryInfo.block_cache_memory;
}


const char*
BlockCacheDataSource::Label() const
{
	return "Block Cache Memory Usage";
}


const char*
BlockCacheDataSource::ShortLabel() const
{
	return "Block Cache";
}


//	#pragma mark -


SemaphoresDataSource::SemaphoresDataSource()
{
	SystemInfo info;

	fMinimum = 0;
	fMaximum = info.MaxSemaphores();

	fColor = (rgb_color){100, 200, 100};
}


SemaphoresDataSource::~SemaphoresDataSource()
{
}


DataSource*
SemaphoresDataSource::Copy() const
{
	return new SemaphoresDataSource(*this);
}


int64
SemaphoresDataSource::NextValue(SystemInfo& info)
{
	return info.UsedSemaphores();
}


const char*
SemaphoresDataSource::Label() const
{
	return "Semaphores";
}


const char*
SemaphoresDataSource::ShortLabel() const
{
	return "Sems";
}


bool
SemaphoresDataSource::AdaptiveScale() const
{
	return true;
}


//	#pragma mark -


PortsDataSource::PortsDataSource()
{
	SystemInfo info;

	fMinimum = 0;
	fMaximum = info.MaxPorts();

	fColor = (rgb_color){180, 200, 180};
}


PortsDataSource::~PortsDataSource()
{
}


DataSource*
PortsDataSource::Copy() const
{
	return new PortsDataSource(*this);
}


int64
PortsDataSource::NextValue(SystemInfo& info)
{
	return info.UsedPorts();
}


const char*
PortsDataSource::Label() const
{
	return "Ports";
}


bool
PortsDataSource::AdaptiveScale() const
{
	return true;
}


//	#pragma mark -


ThreadsDataSource::ThreadsDataSource()
{
	SystemInfo info;

	fMinimum = 0;
	fMaximum = info.MaxThreads();

	fColor = (rgb_color){0, 0, 200};
}


ThreadsDataSource::~ThreadsDataSource()
{
}


DataSource*
ThreadsDataSource::Copy() const
{
	return new ThreadsDataSource(*this);
}


int64
ThreadsDataSource::NextValue(SystemInfo& info)
{
	return info.UsedThreads();
}


const char*
ThreadsDataSource::Label() const
{
	return "Threads";
}


bool
ThreadsDataSource::AdaptiveScale() const
{
	return true;
}


//	#pragma mark -


TeamsDataSource::TeamsDataSource()
{
	SystemInfo info;

	fMinimum = 0;
	fMaximum = info.MaxTeams();

	fColor = (rgb_color){0, 150, 255};
}


TeamsDataSource::~TeamsDataSource()
{
}


DataSource*
TeamsDataSource::Copy() const
{
	return new TeamsDataSource(*this);
}


int64
TeamsDataSource::NextValue(SystemInfo& info)
{
	return info.UsedTeams();
}


const char*
TeamsDataSource::Label() const
{
	return "Teams";
}


bool
TeamsDataSource::AdaptiveScale() const
{
	return true;
}


//	#pragma mark -


RunningAppsDataSource::RunningAppsDataSource()
{
	SystemInfo info;

	fMinimum = 0;
	fMaximum = info.MaxRunningApps();

	fColor = (rgb_color){100, 150, 255};
}


RunningAppsDataSource::~RunningAppsDataSource()
{
}


DataSource*
RunningAppsDataSource::Copy() const
{
	return new RunningAppsDataSource(*this);
}


int64
RunningAppsDataSource::NextValue(SystemInfo& info)
{
	return info.UsedRunningApps();
}


const char*
RunningAppsDataSource::Label() const
{
	return "Running Applications";
}


const char*
RunningAppsDataSource::ShortLabel() const
{
	return "Apps";
}


bool
RunningAppsDataSource::AdaptiveScale() const
{
	return true;
}


//	#pragma mark -


CPUUsageDataSource::CPUUsageDataSource(int32 cpu)
	:
	fPreviousActive(0),
	fPreviousTime(0)
{
	fMinimum = 0;
	fMaximum = 1000;

	_SetCPU(cpu);
}


CPUUsageDataSource::CPUUsageDataSource(const CPUUsageDataSource& other)
	: DataSource(other)
{
	fPreviousActive = other.fPreviousActive;
	fPreviousTime = other.fPreviousTime;
	fCPU = other.fCPU;
	fLabel = other.fLabel;
	fShortLabel = other.fShortLabel;
}


CPUUsageDataSource::~CPUUsageDataSource()
{
}


DataSource*
CPUUsageDataSource::Copy() const
{
	return new CPUUsageDataSource(*this);
}


DataSource*
CPUUsageDataSource::CopyForCPU(int32 cpu) const
{
	CPUUsageDataSource* copy = new CPUUsageDataSource(*this);
	copy->_SetCPU(cpu);

	return copy;
}


void
CPUUsageDataSource::Print(BString& text, int64 value) const
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%.1f%%", value / 10.0);

	text = buffer;
}


int64
CPUUsageDataSource::NextValue(SystemInfo& info)
{
	bigtime_t active = info.Info().cpu_infos[fCPU].active_time;

	int64 percent = int64(1000.0 * (active - fPreviousActive)
		/ (info.Time() - fPreviousTime));
	if (percent < 0)
		percent = 0;
	if (percent > 1000)
		percent = 1000;

	fPreviousActive = active;
	fPreviousTime = info.Time();

	return percent;
}


const char*
CPUUsageDataSource::Label() const
{
	return fLabel.String();
}


const char*
CPUUsageDataSource::ShortLabel() const
{
	return fShortLabel.String();
}


const char*
CPUUsageDataSource::Name() const
{
	return "CPU Usage";
}


int32
CPUUsageDataSource::CPU() const
{
	return fCPU;
}


bool
CPUUsageDataSource::PerCPU() const
{
	return true;
}


bool
CPUUsageDataSource::Primary() const
{
	return true;
}


void
CPUUsageDataSource::_SetCPU(int32 cpu)
{
	fCPU = cpu;
	fLabel = "CPU";
	if (SystemInfo().CPUCount() > 1)
		fLabel << " " << cpu;

	fShortLabel = fLabel;
	fLabel << " Usage";

	const rgb_color kColors[] = {
		// TODO: find some better defaults...
		{200, 0, 200},
		{0, 200, 200},
		{80, 80, 80},
		{230, 150, 50},
	};
	const uint32 kNumColors = sizeof(kColors) / sizeof(kColors[0]);

	fColor = kColors[cpu % kNumColors];
}


//	#pragma mark -


CPUCombinedUsageDataSource::CPUCombinedUsageDataSource()
	:
	fPreviousActive(0),
	fPreviousTime(0)
{
	fMinimum = 0;
	fMaximum = 1000;

	fColor = (rgb_color){200, 200, 0};
}


CPUCombinedUsageDataSource::CPUCombinedUsageDataSource(
		const CPUCombinedUsageDataSource& other)
	: DataSource(other)
{
	fPreviousActive = other.fPreviousActive;
	fPreviousTime = other.fPreviousTime;
}


CPUCombinedUsageDataSource::~CPUCombinedUsageDataSource()
{
}


DataSource*
CPUCombinedUsageDataSource::Copy() const
{
	return new CPUCombinedUsageDataSource(*this);
}


void
CPUCombinedUsageDataSource::Print(BString& text, int64 value) const
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%.1f%%", value / 10.0);

	text = buffer;
}


int64
CPUCombinedUsageDataSource::NextValue(SystemInfo& info)
{
	int32 running = 0;
	bigtime_t active = 0;

	for (int32 cpu = 0; cpu < info.Info().cpu_count; cpu++) {
		active += info.Info().cpu_infos[cpu].active_time;
		running++;
			// TODO: take disabled CPUs into account
	}

	int64 percent = int64(1000.0 * (active - fPreviousActive)
		/ (running * (info.Time() - fPreviousTime)));
	if (percent < 0)
		percent = 0;
	if (percent > 1000)
		percent = 1000;

	fPreviousActive = active;
	fPreviousTime = info.Time();

	return percent;
}


const char*
CPUCombinedUsageDataSource::Label() const
{
	return "CPU Usage";
}


const char*
CPUCombinedUsageDataSource::ShortLabel() const
{
	return "CPU";
}


const char*
CPUCombinedUsageDataSource::Name() const
{
	return "CPU Usage (combined)";
}


bool
CPUCombinedUsageDataSource::MultiCPUOnly() const
{
	return true;
}


bool
CPUCombinedUsageDataSource::Primary() const
{
	return true;
}


//	#pragma mark -


PageFaultsDataSource::PageFaultsDataSource()
	:
	fPreviousFaults(0),
	fPreviousTime(0)
{
	SystemInfo info;
	NextValue(info);

	fMinimum = 0;
	fMaximum = 1000000000LL;

	fColor = (rgb_color){200, 0, 150, 0};
}


PageFaultsDataSource::PageFaultsDataSource(const PageFaultsDataSource& other)
	: DataSource(other)
{
	fPreviousFaults = other.fPreviousFaults;
	fPreviousTime = other.fPreviousTime;
}


PageFaultsDataSource::~PageFaultsDataSource()
{
}


DataSource*
PageFaultsDataSource::Copy() const
{
	return new PageFaultsDataSource(*this);
}


void
PageFaultsDataSource::Print(BString& text, int64 value) const
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%.1f faults/s", value / 1024.0);

	text = buffer;
}


int64
PageFaultsDataSource::NextValue(SystemInfo& info)
{
	uint64 faults = info.PageFaults();

	int64 faultsPerSecond = uint64(1024 * double(faults - fPreviousFaults)
		/ (info.Time() - fPreviousTime) * 1000000.0);

	fPreviousFaults = faults;
	fPreviousTime = info.Time();

	return faultsPerSecond;
}


const char*
PageFaultsDataSource::Label() const
{
	return "Page Faults";
}


const char*
PageFaultsDataSource::ShortLabel() const
{
	return "P-Faults";
}


const char*
PageFaultsDataSource::Name() const
{
	return "Page Faults";
}


bool
PageFaultsDataSource::AdaptiveScale() const
{
	return true;
}


bool
PageFaultsDataSource::Primary() const
{
	return false;
}


//	#pragma mark -


NetworkUsageDataSource::NetworkUsageDataSource(bool in)
	:
	fIn(in),
	fPreviousBytes(0),
	fPreviousTime(0)
{
	SystemInfo info;
	NextValue(info);

	fMinimum = 0;
	fMaximum = 1000000000LL;

	fColor = fIn ? (rgb_color){200, 150, 0} : (rgb_color){200, 220, 0};
}


NetworkUsageDataSource::NetworkUsageDataSource(
		const NetworkUsageDataSource& other)
	: DataSource(other)
{
	fIn = other.fIn;
	fPreviousBytes = other.fPreviousBytes;
	fPreviousTime = other.fPreviousTime;
}


NetworkUsageDataSource::~NetworkUsageDataSource()
{
}


DataSource*
NetworkUsageDataSource::Copy() const
{
	return new NetworkUsageDataSource(*this);
}


void
NetworkUsageDataSource::Print(BString& text, int64 value) const
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%.1f KB/s", value / 1024.0);

	text = buffer;
}


int64
NetworkUsageDataSource::NextValue(SystemInfo& info)
{
	uint64 transferred = fIn ? info.NetworkReceived() : info.NetworkSent();

	int64 bytesPerSecond = uint64(double(transferred - fPreviousBytes)
		/ (info.Time() - fPreviousTime) * 1000000.0);

	fPreviousBytes = transferred;
	fPreviousTime = info.Time();

	return bytesPerSecond;
}


const char*
NetworkUsageDataSource::Label() const
{
	return fIn ? "Receiving" : "Sending";
}


const char*
NetworkUsageDataSource::ShortLabel() const
{
	return fIn ? "RX" : "TX";
}


const char*
NetworkUsageDataSource::Name() const
{
	return fIn ? "Network Receive" : "Network Send";
}


bool
NetworkUsageDataSource::AdaptiveScale() const
{
	return true;
}


scale_type
NetworkUsageDataSource::ScaleType() const
{
	return kBytePerSecondScale;
}


bool
NetworkUsageDataSource::Primary() const
{
	return true;
}


//	#pragma mark -


ClipboardSizeDataSource::ClipboardSizeDataSource(bool text)
{
	fMinimum = 0;
	fMaximum = UINT32_MAX;
	fText = text;

	fColor = (rgb_color){0, 150, 255};
}


ClipboardSizeDataSource::ClipboardSizeDataSource(
		const ClipboardSizeDataSource& other)
	: DataSource(other)
{
	fText = other.fText;
}


ClipboardSizeDataSource::~ClipboardSizeDataSource()
{
}


DataSource*
ClipboardSizeDataSource::Copy() const
{
	return new ClipboardSizeDataSource(*this);
}


int64
ClipboardSizeDataSource::NextValue(SystemInfo& info)
{
	if (fText)
		return info.ClipboardTextSize()/* / 1024*/;
	return info.ClipboardSize()/* / 1024*/;
}


const char*
ClipboardSizeDataSource::Label() const
{
	return fText ? "Text Clipboard Size" : "Raw Clipboard Size";
}


const char*
ClipboardSizeDataSource::Unit() const
{
	return "bytes"/*"KB"*/;
}


bool
ClipboardSizeDataSource::AdaptiveScale() const
{
	return true;
}


//	#pragma mark -


MediaNodesDataSource::MediaNodesDataSource()
{
	SystemInfo info;

	fMinimum = 0;
	fMaximum = INT32_MAX;

	fColor = (rgb_color){255, 150, 225};
}


MediaNodesDataSource::~MediaNodesDataSource()
{
}


DataSource*
MediaNodesDataSource::Copy() const
{
	return new MediaNodesDataSource(*this);
}


int64
MediaNodesDataSource::NextValue(SystemInfo& info)
{
	return info.MediaNodes();
}


const char*
MediaNodesDataSource::Label() const
{
	return "Media Nodes";
}


bool
MediaNodesDataSource::AdaptiveScale() const
{
	return true;
}


