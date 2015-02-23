// vim: set sts=2 ts=8 sw=2 tw=99 noet:
// 
// Copyright (C) 2006-2015 AlliedModders LLC
// 
// This file is part of SourcePawn. SourcePawn is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// You should have received a copy of the GNU General Public License along with
// SourcePawn. If not, see http://www.gnu.org/licenses/.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine2.h"
#include "x86/jit_x86.h"
#include "zlib/zlib.h"
#include "BaseRuntime.h"
#include "sp_vm_engine.h"
#include "watchdog_timer.h"
#include <sourcemod_version.h>

using namespace SourcePawn;

SourcePawnEngine2::SourcePawnEngine2()
{
  profiler_ = NULL;
  jit_enabled_ = true;
}

IPluginRuntime *
SourcePawnEngine2::LoadPlugin(ICompilation *co, const char *file, int *err)
{
  sp_file_hdr_t hdr;
  uint8_t *base;
  int z_result;
  int error;
  size_t ignore;
  BaseRuntime *pRuntime;

  FILE *fp = fopen(file, "rb");

  if (!fp) {
    error = SP_ERROR_NOT_FOUND;
    goto return_error;
  }

  /* Rewind for safety */
  ignore = fread(&hdr, sizeof(sp_file_hdr_t), 1, fp);

  if (hdr.magic != SmxConsts::FILE_MAGIC) {
    error = SP_ERROR_FILE_FORMAT;
    goto return_error;
  }

  switch (hdr.compression)
  {
  case SmxConsts::FILE_COMPRESSION_GZ:
    {
      uint32_t uncompsize = hdr.imagesize - hdr.dataoffs;
      uint32_t compsize = hdr.disksize - hdr.dataoffs;
      uint32_t sectsize = hdr.dataoffs - sizeof(sp_file_hdr_t);
      uLongf destlen = uncompsize;

      char *tempbuf = (char *)malloc(compsize);
      void *uncompdata = malloc(uncompsize);
      void *sectheader = malloc(sectsize);

      ignore = fread(sectheader, sectsize, 1, fp);
      ignore = fread(tempbuf, compsize, 1, fp);

      z_result = uncompress((Bytef *)uncompdata, &destlen, (Bytef *)tempbuf, compsize);
      free(tempbuf);
      if (z_result != Z_OK)
      {
        free(sectheader);
        free(uncompdata);
        error = SP_ERROR_DECOMPRESSOR;
        goto return_error;
      }

      base = (uint8_t *)malloc(hdr.imagesize);
      memcpy(base, &hdr, sizeof(sp_file_hdr_t));
      memcpy(base + sizeof(sp_file_hdr_t), sectheader, sectsize);
      free(sectheader);
      memcpy(base + hdr.dataoffs, uncompdata, uncompsize);
      free(uncompdata);
      break;
    }
  case SmxConsts::FILE_COMPRESSION_NONE:
    {
      base = (uint8_t *)malloc(hdr.imagesize);
      rewind(fp);
      ignore = fread(base, hdr.imagesize, 1, fp);
      break;
    }
  default:
    {
      error = SP_ERROR_DECOMPRESSOR;
      goto return_error;
    }
  }

  pRuntime = new BaseRuntime();
  if ((error = pRuntime->CreateFromMemory(&hdr, base)) != SP_ERROR_NONE) {
    delete pRuntime;
    goto return_error;
  }

  size_t len;
  
  len = strlen(file);
  for (size_t i = len - 1; i < len; i--)
  {
    if (file[i] == '/' 
    #if defined WIN32
      || file[i] == '\\'
    #endif
    )
    {
      pRuntime->SetName(&file[i+1]);
      break;
    }
  }

  (void)ignore;

  if (!pRuntime->plugin()->name)
    pRuntime->SetName(file);

  pRuntime->ApplyCompilationOptions(co);

  fclose(fp);

  return pRuntime;

return_error:
  *err = error;
  if (fp != NULL)
  {
      fclose(fp);
  }

  return NULL;
}

SPVM_NATIVE_FUNC
SourcePawnEngine2::CreateFakeNative(SPVM_FAKENATIVE_FUNC callback, void *pData)
{
  return g_Jit.CreateFakeNative(callback, pData);
}

void
SourcePawnEngine2::DestroyFakeNative(SPVM_NATIVE_FUNC func)
{
  g_Jit.DestroyFakeNative(func);
}

const char *
SourcePawnEngine2::GetEngineName()
{
  return "SourcePawn 1.7, jit-x86";
}

const char *
SourcePawnEngine2::GetVersionString()
{
  return SOURCEMOD_VERSION;
}

IDebugListener *
SourcePawnEngine2::SetDebugListener(IDebugListener *listener)
{
  return g_engine1.SetDebugListener(listener);
}

unsigned int
SourcePawnEngine2::GetAPIVersion()
{
  return SOURCEPAWN_ENGINE2_API_VERSION;
}

ICompilation *
SourcePawnEngine2::StartCompilation()
{
  return g_Jit.StartCompilation();
}

const char *
SourcePawnEngine2::GetErrorString(int err)
{
  return g_engine1.GetErrorString(err);
}

bool
SourcePawnEngine2::Initialize()
{
  return g_Jit.InitializeJIT();
}

void
SourcePawnEngine2::Shutdown()
{
  g_WatchdogTimer.Shutdown();
  g_Jit.ShutdownJIT();
}

IPluginRuntime *
SourcePawnEngine2::CreateEmptyRuntime(const char *name, uint32_t memory)
{
  int err;

  BaseRuntime *rt = new BaseRuntime();
  if ((err = rt->CreateBlank(memory)) != SP_ERROR_NONE) {
    delete rt;
    return NULL;
  }

  rt->SetName(name != NULL ? name : "<anonymous>");

  rt->ApplyCompilationOptions(NULL);
  
  return rt;
}

bool
SourcePawnEngine2::InstallWatchdogTimer(size_t timeout_ms)
{
  return g_WatchdogTimer.Initialize(timeout_ms);
}

