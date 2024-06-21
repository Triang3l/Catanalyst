#include "Catanalyst.h"

#include <Windows.h>

#include <d3dkmthk.h>

#include "../Detours/src/detours.h"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

static void KMTI_PrintArray(char const * const name, void const * const data,
                            std::size_t const size) {
   printf("  %s = 0x%p:", name, data);
   for (size_t byte_index = 0; byte_index < size; ++byte_index) {
      if ((byte_index & 0xF) == 0) {
         fputs("\n  ", stdout);
      }
      if ((byte_index & 0x3) == 0) {
         putchar(' ');
      }
      printf(" %02X,", static_cast<unsigned char const *>(data)[byte_index]);
   }
   putchar('\n');
}

struct KMTI_Context {
   UINT32 node_ordinal;
   void * command_buffer;
   D3DDDI_ALLOCATIONLIST * allocation_list;
   D3DDDI_PATCHLOCATIONLIST * patch_location_list;
};

static std::shared_mutex kmti_context_mutex;
static std::unordered_map<D3DKMT_HANDLE, KMTI_Context> kmti_contexts;

// D3DKMTEscape

static NTSTATUS (APIENTRY * Real_NtGdiDdDDIEscape)(D3DKMT_ESCAPE *);

static NTSTATUS APIENTRY Catch_NtGdiDdDDIEscape(D3DKMT_ESCAPE * const escape_data)
{
   printf("NtGdiDdDDIEscape @ %" PRIu32 ":\n", GetThreadId(GetCurrentThread()));
   printf("  > hAdapter = 0x%X\n", escape_data->hAdapter);
   printf("  > hDevice = 0x%X\n", escape_data->hDevice);
   printf("  > Type = %u\n", escape_data->Type);
   printf("  > Flags = 0x%08X\n", escape_data->Flags.Value);
   KMTI_PrintArray("> pPrivateDriverData", escape_data->pPrivateDriverData,
                   escape_data->PrivateDriverDataSize);
   printf("  > PrivateDriverDataSize = 0x%X\n", escape_data->PrivateDriverDataSize);
   printf("  > hContext = 0x%X\n", escape_data->hContext);
   NTSTATUS const status = Real_NtGdiDdDDIEscape(escape_data);
   printf("    Status = 0x%08lX\n", status);
   KMTI_PrintArray("< pPrivateDriverData", escape_data->pPrivateDriverData,
                   escape_data->PrivateDriverDataSize);
   putchar('\n');
   return status;
}

// D3DKMTQueryAdapterInfo

static NTSTATUS (APIENTRY * Real_NtGdiDdDDIQueryAdapterInfo)(D3DKMT_QUERYADAPTERINFO *);

static NTSTATUS APIENTRY Catch_NtGdiDdDDIQueryAdapterInfo(
   D3DKMT_QUERYADAPTERINFO * const query_adapter_info_data)
{
   printf("NtGdiDdDDIQueryAdapterInfo @ %" PRIu32 ":\n", GetThreadId(GetCurrentThread()));
   printf("  > hAdapter = 0x%X\n", query_adapter_info_data->hAdapter);
   printf("  > Type = %u\n", query_adapter_info_data->Type);
   KMTI_PrintArray("> pPrivateDriverData", query_adapter_info_data->pPrivateDriverData,
                   query_adapter_info_data->PrivateDriverDataSize);
   printf("  > PrivateDriverDataSize = 0x%X\n", query_adapter_info_data->PrivateDriverDataSize);
   NTSTATUS const status = Real_NtGdiDdDDIQueryAdapterInfo(query_adapter_info_data);
   printf("    Status = 0x%08lX\n", status);
   KMTI_PrintArray("< pPrivateDriverData", query_adapter_info_data->pPrivateDriverData,
                   query_adapter_info_data->PrivateDriverDataSize);
   printf("  < PrivateDriverDataSize = 0x%X\n", query_adapter_info_data->PrivateDriverDataSize);
   putchar('\n');
   return status;
}

// D3DKMTCreateDevice

static NTSTATUS (APIENTRY * Real_NtGdiDdDDICreateDevice)(D3DKMT_CREATEDEVICE *);

static NTSTATUS APIENTRY Catch_NtGdiDdDDICreateDevice(
   D3DKMT_CREATEDEVICE * const create_device_data) {
   printf("NtGdiDdDDICreateDevice @ %" PRIu32 ":\n", GetThreadId(GetCurrentThread()));
   printf("  > hAdapter = 0x%X\n", create_device_data->hAdapter);
   printf("  > Flags.LegacyMode = %u\n", create_device_data->Flags.LegacyMode);
   printf("  > Flags.RequestVSync = %u\n", create_device_data->Flags.RequestVSync);
   printf("  > Flags.DisableGpuTimeout = %u\n", create_device_data->Flags.DisableGpuTimeout);
   NTSTATUS const status = Real_NtGdiDdDDICreateDevice(create_device_data);
   printf("    Status = 0x%08lX\n", status);
   printf("  < hDevice = 0x%X\n", create_device_data->hDevice);
   printf("  < pCommandBuffer = 0x%p\n", create_device_data->pCommandBuffer);
   printf("  < CommandBufferSize = 0x%X\n", create_device_data->CommandBufferSize);
   printf("  < pAllocationList = 0x%p\n", create_device_data->pAllocationList);
   printf("  < AllocationListSize = 0x%X\n", create_device_data->AllocationListSize);
   printf("  < pPatchLocationList = 0x%p\n", create_device_data->pPatchLocationList);
   printf("  < PatchLocationListSize = 0x%X\n", create_device_data->PatchLocationListSize);
   putchar('\n');
   return status;
}

// D3DKMTCreateSynchronizationObject2

static NTSTATUS (APIENTRY * Real_NtGdiDdDDICreateSynchronizationObject)(
   D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *);

static NTSTATUS APIENTRY Catch_NtGdiDdDDICreateSynchronizationObject(
   D3DKMT_CREATESYNCHRONIZATIONOBJECT2 * const create_synchronization_object_data) {
   printf("NtGdiDdDDICreateSynchronizationObject @ %" PRIu32 ":\n", GetThreadId(GetCurrentThread()));
   printf("  > hDevice = 0x%X\n", create_synchronization_object_data->hDevice);
   printf("  > Info.Type = %u\n", create_synchronization_object_data->Info.Type);
   NTSTATUS const status =
      Real_NtGdiDdDDICreateSynchronizationObject(create_synchronization_object_data);
   printf("    Status = 0x%08lX\n", status);
   printf("  < Info.Type = %u\n", create_synchronization_object_data->Info.Type);
   printf("  < hSyncObject = 0x%X\n", create_synchronization_object_data->hSyncObject);
   putchar('\n');
   return status;
}

// D3DKMTCreateAllocation2

static NTSTATUS (APIENTRY * Real_NtGdiDdDDICreateAllocation)(D3DKMT_CREATEALLOCATION *);

static NTSTATUS APIENTRY Catch_NtGdiDdDDICreateAllocation(
   D3DKMT_CREATEALLOCATION * const create_allocation_data) {
   printf("NtGdiDdDDICreateAllocation @ %" PRIu32 ":\n", GetThreadId(GetCurrentThread()));
   printf("  > hDevice = 0x%X\n", create_allocation_data->hDevice);
   printf("  > hResource = 0x%X\n", create_allocation_data->hResource);
   KMTI_PrintArray("> pPrivateRuntimeData", create_allocation_data->pPrivateRuntimeData,
                   create_allocation_data->PrivateRuntimeDataSize);
   printf("  > PrivateRuntimeDataSize = 0x%X\n", create_allocation_data->PrivateRuntimeDataSize);
   KMTI_PrintArray("> pPrivateDriverData", create_allocation_data->pPrivateDriverData,
                   create_allocation_data->PrivateDriverDataSize);
   printf("  > PrivateDriverDataSize = 0x%X\n", create_allocation_data->PrivateDriverDataSize);
   printf("  > NumAllocations = %u\n", create_allocation_data->NumAllocations);
   puts("  > pAllocationInfo2:");
   for (UINT allocation_index = 0; allocation_index < create_allocation_data->NumAllocations;
        ++allocation_index) {
      printf("    [%u]:\n", allocation_index);
      D3DDDI_ALLOCATIONINFO2 const * const allocation_info =
         &create_allocation_data->pAllocationInfo2[allocation_index];
      printf("      hSection = 0x%p\n", allocation_info->hSection);
      KMTI_PrintArray("    pPrivateDriverData", allocation_info->pPrivateDriverData,
                      allocation_info->PrivateDriverDataSize);
      printf("      PrivateDriverDataSize = 0x%X\n", allocation_info->PrivateDriverDataSize);
      printf("      VidPnSourceId = 0x%X\n", allocation_info->VidPnSourceId);
      printf("      Flags.Primary = %u\n", allocation_info->Flags.Primary);
      printf("      Flags.Stereo = %u\n", allocation_info->Flags.Stereo);
   }
   printf("  > Flags.CreateResource = %u\n", create_allocation_data->Flags.CreateResource);
   printf("  > Flags.CreateShared = %u\n", create_allocation_data->Flags.CreateShared);
   printf("  > Flags.NonSecure = %u\n", create_allocation_data->Flags.NonSecure);
   printf("  > Flags.CreateProtected (KM) = %u\n", create_allocation_data->Flags.CreateProtected);
   printf("  > Flags.RestrictSharedAccess = %u\n",
          create_allocation_data->Flags.RestrictSharedAccess);
   printf("  > Flags.ExistingSysMem (KM) = %u\n", create_allocation_data->Flags.ExistingSysMem);
   printf("  > Flags.NtSecuritySharing = %u\n", create_allocation_data->Flags.NtSecuritySharing);
   printf("  > Flags.ReadOnly = %u\n", create_allocation_data->Flags.ReadOnly);
   printf("  > Flags.CreateWriteCombined (KM) = %u\n",
          create_allocation_data->Flags.CreateWriteCombined);
   printf("  > Flags.CreateCached (KM) = %u\n", create_allocation_data->Flags.CreateCached);
   printf("  > Flags.SwapChainBackBuffer = %u\n",
          create_allocation_data->Flags.SwapChainBackBuffer);
   printf("  > Flags.CrossAdapter = %u\n", create_allocation_data->Flags.CrossAdapter);
   printf("  > Flags.OpenCrossAdapter (KM) = %u\n", create_allocation_data->Flags.OpenCrossAdapter);
   printf("  > Flags.PartialSharedCreation = %u\n",
          create_allocation_data->Flags.PartialSharedCreation);
   printf("  > Flags.WriteWatch = %u\n", create_allocation_data->Flags.WriteWatch);
   printf("  > hPrivateRuntimeResourceHandle = 0x%p\n",
          create_allocation_data->hPrivateRuntimeResourceHandle);
   NTSTATUS const status = Real_NtGdiDdDDICreateAllocation(create_allocation_data);
   printf("    Status = 0x%08lX\n", status);
   printf("  < hResource = 0x%X\n", create_allocation_data->hResource);
   printf("  < hGlobalShare = 0x%X\n", create_allocation_data->hGlobalShare);
   puts("  < pAllocationInfo2:");
   for (UINT allocation_index = 0; allocation_index < create_allocation_data->NumAllocations;
        ++allocation_index) {
      printf("    [%u]:\n", allocation_index);
      D3DDDI_ALLOCATIONINFO2 const * const allocation_info =
         &create_allocation_data->pAllocationInfo2[allocation_index];
      printf("      hAllocation = 0x%X\n", allocation_info->hAllocation);
      KMTI_PrintArray("    pPrivateDriverData", allocation_info->pPrivateDriverData,
                      allocation_info->PrivateDriverDataSize);
   }
   printf("  < hPrivateRuntimeResourceHandle = 0x%p\n",
          create_allocation_data->hPrivateRuntimeResourceHandle);
   putchar('\n');
   return status;
}

// D3DKMTLock

static NTSTATUS (APIENTRY * Real_NtGdiDdDDILock)(D3DKMT_LOCK *);

static NTSTATUS APIENTRY Catch_NtGdiDdDDILock(D3DKMT_LOCK * const lock_data) {
   printf("NtGdiDdDDILock @ %" PRIu32 ":\n", GetThreadId(GetCurrentThread()));
   printf("  > hDevice = 0x%X\n", lock_data->hDevice);
   printf("  > hAllocation = 0x%X\n", lock_data->hAllocation);
   printf("  > PrivateDriverData = 0x%X\n", lock_data->PrivateDriverData);
   printf("  > NumPages = %u\n", lock_data->NumPages);
   for (UINT page_index = 0; page_index < lock_data->NumPages; ++page_index) {
      printf("    [0x%X] = 0x%X\n", page_index, lock_data->pPages[page_index]);
   }
   printf("  > Flags.ReadOnly = %u\n", lock_data->Flags.ReadOnly);
   printf("  > Flags.WriteOnly = %u\n", lock_data->Flags.WriteOnly);
   printf("  > Flags.DonotWait = %u\n", lock_data->Flags.DonotWait);
   printf("  > Flags.IgnoreSync = %u\n", lock_data->Flags.IgnoreSync);
   printf("  > Flags.LockEntire = %u\n", lock_data->Flags.LockEntire);
   printf("  > Flags.DonotEvict = %u\n", lock_data->Flags.DonotEvict);
   printf("  > Flags.AcquireAperture = %u\n", lock_data->Flags.AcquireAperture);
   printf("  > Flags.Discard = %u\n", lock_data->Flags.Discard);
   printf("  > Flags.NoExistingReference = %u\n", lock_data->Flags.NoExistingReference);
   printf("  > Flags.UseAlternateVA = %u\n", lock_data->Flags.UseAlternateVA);
   printf("  > Flags.IgnoreReadSync = %u\n", lock_data->Flags.IgnoreReadSync);
   NTSTATUS const status = Real_NtGdiDdDDILock(lock_data);
   printf("    Status = 0x%08lX\n", status);
   printf("  < pData = 0x%p\n", lock_data->pData);
   printf("  < GpuVirtualAddress = 0x%llX\n", lock_data->GpuVirtualAddress);
   putchar('\n');
   return status;
}

// D3DKMTCreateContext

static NTSTATUS (APIENTRY * Real_NtGdiDdDDICreateContext)(D3DKMT_CREATECONTEXT *);

struct KMTI_ContextPrivateDriverData {
   uint32_t private_driver_data_size; // Possibly. 0x40.
   uint32_t unknown_0x4; // 02 20 00 00 on Barts.
   // 0x10000 for the first NodeOrdinal = 0 context (graphics?)
   // 0x4000 for the second NodeOrdinal = 0 context (texture initial data GFX?)
   // 0x1000 for the NodeOrdinal = 1 context (SDMA?)
   uint32_t command_buffer_size;
   uint32_t allocation_list_size;
   uint32_t patch_location_list_size;
   uint32_t unknown_0x14[(0x40 - 0x14) / sizeof(uint32_t)]; // Zeros.
};

static NTSTATUS APIENTRY Catch_NtGdiDdDDICreateContext(
   D3DKMT_CREATECONTEXT * const create_context_data) {
   printf("NtGdiDdDDICreateContext @ %" PRIu32 ":\n", GetThreadId(GetCurrentThread()));
   printf("  > hDevice = 0x%X\n", create_context_data->hDevice);
   // 0 - GFX?
   // 1 - SDMA?
   printf("  > NodeOrdinal = %u\n", create_context_data->NodeOrdinal);
   // 0x1.
   printf("  > EngineAffinity = 0x%X\n", create_context_data->EngineAffinity);
   printf("  > Flags = 0x%X\n", create_context_data->Flags.Value);
   KMTI_PrintArray("> pPrivateDriverData", create_context_data->pPrivateDriverData,
                   create_context_data->PrivateDriverDataSize);
   printf("  > PrivateDriverDataSize = 0x%X\n", create_context_data->PrivateDriverDataSize);
   // The Direct3D 11 driver passes 10 (Direct3D 10).
   printf("  > ClientHint = %u\n", create_context_data->ClientHint);
   NTSTATUS const status = Real_NtGdiDdDDICreateContext(create_context_data);
   printf("    Status = 0x%08lX\n", status);
   printf("  < hContext = 0x%X\n", create_context_data->hContext);
   printf("  < pCommandBuffer = 0x%p\n", create_context_data->pCommandBuffer);
   printf("  < CommandBufferSize = 0x%X\n", create_context_data->CommandBufferSize);
   printf("  < pAllocationList = 0x%p\n", create_context_data->pAllocationList);
   printf("  < AllocationListSize = 0x%X\n", create_context_data->AllocationListSize);
   printf("  < pPatchLocationList = 0x%p\n", create_context_data->pPatchLocationList);
   printf("  < PatchLocationListSize = 0x%X\n", create_context_data->PatchLocationListSize);
   printf("  < CommandBuffer = 0x%llX\n", create_context_data->CommandBuffer);
   putchar('\n');
   if (status == 0) {
      std::unique_lock<std::shared_mutex> context_lock(kmti_context_mutex);
      KMTI_Context & context =
         kmti_contexts.emplace(create_context_data->hContext, KMTI_Context()).first->second;
      context.node_ordinal = create_context_data->NodeOrdinal;
      context.command_buffer = create_context_data->pCommandBuffer;
      context.allocation_list = create_context_data->pAllocationList;
      context.patch_location_list = create_context_data->pPatchLocationList;
   }
   return status;
}

// D3DKMTSetContextSchedulingPriority

static NTSTATUS (APIENTRY * Real_NtGdiDdDDISetContextSchedulingPriority)(
   D3DKMT_SETCONTEXTSCHEDULINGPRIORITY const *);

static NTSTATUS APIENTRY Catch_NtGdiDdDDISetContextSchedulingPriority(
   D3DKMT_SETCONTEXTSCHEDULINGPRIORITY const * const set_scheduling_priority_data)
{
   printf("NtGdiDdDDISetContextSchedulingPriority @ %" PRIu32 ":\n", GetThreadId(GetCurrentThread()));
   printf("  > hContext = 0x%X\n", set_scheduling_priority_data->hContext);
   printf("  > Priority = %d\n", set_scheduling_priority_data->Priority);
   NTSTATUS const status =
      Real_NtGdiDdDDISetContextSchedulingPriority(set_scheduling_priority_data);
   printf("    Status = 0x%08lX\n", status);
   putchar('\n');
   return status;
}

// D3DKMTRender

static NTSTATUS (APIENTRY * Real_NtGdiDdDDIRender)(D3DKMT_RENDER *);

static NTSTATUS APIENTRY Catch_NtGdiDdDDIRender(D3DKMT_RENDER * const render_data) {
   std::optional<KMTI_Context> context;
   {
      std::shared_lock<std::shared_mutex> context_lock(kmti_context_mutex);
      auto const context_iterator = kmti_contexts.find(render_data->hContext);
      if (context_iterator != kmti_contexts.end()) {
         context = context_iterator->second;
      }
   }
   printf("NtGdiDdDDIRender @ %" PRIu32 ":\n", GetThreadId(GetCurrentThread()));
   printf("  > hContext = 0x%X\n", render_data->hContext);
   printf("  > CommandOffset = 0x%X\n", render_data->CommandOffset);
   printf("  > CommandLength = 0x%X\n", render_data->CommandLength);
   if (context) {
      void const * const command =
         static_cast<char const *>(context->command_buffer) + render_data->CommandOffset;
      KMTI_PrintArray("> pCommandBuffer", command, render_data->CommandLength);
      if (context->node_ordinal == 0) {
         PM4P_Print(static_cast<uint32_t const *>(command),
                    render_data->CommandLength / sizeof(uint32_t), false);
      }
   }
   printf("  > AllocationCount = %u\n", render_data->AllocationCount);
   if (context) {
      for (UINT allocation_index = 0; allocation_index < render_data->AllocationCount;
           ++allocation_index) {
         D3DDDI_ALLOCATIONLIST const & allocation = context->allocation_list[allocation_index];
         printf("    [%u] = 0x%X, flags %X\n", allocation_index, allocation.hAllocation,
                allocation.Value);
      }
   }
   printf("  > PatchLocationCount = %u\n", render_data->PatchLocationCount);
   if (context) {
      for (UINT patch_location_index = 0; patch_location_index < render_data->PatchLocationCount;
           ++patch_location_index) {
         D3DDDI_PATCHLOCATIONLIST const & patch_location =
            context->patch_location_list[patch_location_index];
         printf(
            "    [%u] = allocation %u, slot 0x%X << 10 | 0x%X (0x%X), driver ID 0x%X, allocation "
            "offset 0x%X, patch offset 0x%X, split offset 0x%X\n",
            patch_location_index, patch_location.AllocationIndex, patch_location.SlotId >> 10,
            patch_location.SlotId & ((UINT(1) << 10) - 1), patch_location.SlotId,
            patch_location.DriverId, patch_location.AllocationOffset, patch_location.PatchOffset,
            patch_location.SplitOffset);
      }
   }
   printf("  > NewCommandBufferSize = 0x%X\n", render_data->NewCommandBufferSize);
   printf("  > NewAllocationListSize = 0x%X\n", render_data->NewAllocationListSize);
   printf("  > NewPatchLocationListSize = 0x%X\n", render_data->NewPatchLocationListSize);
   printf("  > Flags.ResizeCommandBuffer = %u\n", render_data->Flags.ResizeCommandBuffer);
   printf("  > Flags.ResizeAllocationList = %u\n", render_data->Flags.ResizeAllocationList);
   printf("  > Flags.ResizePatchLocationList = %u\n", render_data->Flags.ResizePatchLocationList);
   printf("  > Flags.NullRendering = %u\n", render_data->Flags.NullRendering);
   printf("  > Flags.PresentRedirected = %u\n", render_data->Flags.PresentRedirected);
   printf("  > Flags.RenderKm = %u\n", render_data->Flags.RenderKm);
   printf("  > Flags.RenderKmReadback = %u\n", render_data->Flags.RenderKmReadback);
   printf("  > PresentHistoryToken = 0x%llX\n", render_data->PresentHistoryToken);
   printf("  > BroadcastContextCount = %lu\n", render_data->BroadcastContextCount);
   for (ULONG broadcast_context_index = 0;
        broadcast_context_index < render_data->BroadcastContextCount; ++broadcast_context_index) {
      printf("  > BroadcastContext[%lu] = 0x%X\n", broadcast_context_index,
             render_data->BroadcastContext[broadcast_context_index]);
   }
   KMTI_PrintArray("> pPrivateDriverData", render_data->pPrivateDriverData,
                   render_data->PrivateDriverDataSize);
   printf("  > PrivateDriverDataSize = 0x%X\n", render_data->PrivateDriverDataSize);
   NTSTATUS const status = Real_NtGdiDdDDIRender(render_data);
   printf("    Status = 0x%08lX\n", status);
   printf("  < pNewCommandBuffer = 0x%p\n", render_data->pNewCommandBuffer);
   printf("  < NewCommandBufferSize = 0x%X\n", render_data->NewCommandBufferSize);
   printf("  < pNewAllocationList = 0x%p\n", render_data->pNewAllocationList);
   printf("  < NewAllocationListSize = 0x%X\n", render_data->NewAllocationListSize);
   printf("  < pNewPatchLocationList = 0x%p\n", render_data->pNewPatchLocationList);
   printf("  < NewPatchLocationListSize = 0x%X\n", render_data->NewPatchLocationListSize);
   printf("  < QueuedBufferCount = %lu\n", render_data->QueuedBufferCount);
   printf("  < NewCommandBuffer = 0x%llX\n", render_data->NewCommandBuffer);
   putchar('\n');
   if (status == 0) {
      std::unique_lock<std::shared_mutex> context_lock(kmti_context_mutex);
      auto const context_iterator = kmti_contexts.find(render_data->hContext);
      if (context_iterator != kmti_contexts.end()) {
         KMTI_Context & update_context = context_iterator->second;
         update_context.command_buffer = render_data->pNewCommandBuffer;
         update_context.allocation_list = render_data->pNewAllocationList;
         update_context.patch_location_list = render_data->pNewPatchLocationList;
      }
   }
   return status;
}

void KMTI_Begin() {
   DetourTransactionBegin();
   DetourUpdateThread(GetCurrentThread());
#define KMTI_ATTACH(name) \
   Real_ ## name = (decltype(Real_ ## name))DetourFindFunction("win32u.dll", #name); \
   DetourAttach(&(PVOID &)Real_ ## name, Catch_ ## name);
   KMTI_ATTACH(NtGdiDdDDICreateAllocation)
   KMTI_ATTACH(NtGdiDdDDICreateContext)
   KMTI_ATTACH(NtGdiDdDDICreateDevice)
   KMTI_ATTACH(NtGdiDdDDICreateSynchronizationObject)
   KMTI_ATTACH(NtGdiDdDDIEscape)
   KMTI_ATTACH(NtGdiDdDDILock)
   KMTI_ATTACH(NtGdiDdDDIQueryAdapterInfo)
   KMTI_ATTACH(NtGdiDdDDIRender)
   KMTI_ATTACH(NtGdiDdDDISetContextSchedulingPriority)
   DetourTransactionCommit();
}
