/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define _DEFAULT_SOURCE
#include "dnodeDnode.h"
#include "dnodeMnode.h"
#include "dnodeTransport.h"
#include "dnodeVnodes.h"
#include "sync.h"
#include "tcache.h"
#include "tconfig.h"
#include "tnote.h"
#include "tstep.h"
#include "wal.h"

static struct {
  SStartupMsg startup;
  EDnStat     runStat;
  SSteps     *steps;
} tsInt;

EDnStat dnodeGetRunStat() { return tsInt.runStat; }

void dnodeSetRunStat(EDnStat stat) { tsInt.runStat = stat; }

void dnodeReportStartup(char *name, char *desc) {
  SStartupMsg *pStartup = &tsInt.startup;
  tstrncpy(pStartup->name, name, strlen(pStartup->name));
  tstrncpy(pStartup->desc, desc, strlen(pStartup->desc));
  pStartup->finished = 0;
}

void dnodeReportStartupFinished(char *name, char *desc) {
  SStartupMsg *pStartup = &tsInt.startup;
  tstrncpy(pStartup->name, name, strlen(pStartup->name));
  tstrncpy(pStartup->desc, desc, strlen(pStartup->desc));
  pStartup->finished = 1;
}

void dnodeGetStartup(SStartupMsg *pStartup) { memcpy(pStartup, &tsInt.startup, sizeof(SStartupMsg)); }

static int32_t dnodeCheckRunning(char *dir) {
  char filepath[256] = {0};
  snprintf(filepath, sizeof(filepath), "%s/.running", dir);

  FileFd fd = taosOpenFileCreateWriteTrunc(filepath);
  if (fd < 0) {
    dError("failed to open lock file:%s since %s, quit", filepath, strerror(errno));
    return -1;
  }

  int32_t ret = taosLockFile(fd);
  if (ret != 0) {
    dError("failed to lock file:%s since %s, quit", filepath, strerror(errno));
    taosCloseFile(fd);
    return -1;
  }

  return 0;
}

static int32_t dnodeInitDir() {
  sprintf(tsMnodeDir, "%s/mnode", tsDataDir);
  sprintf(tsVnodeDir, "%s/vnode", tsDataDir);
  sprintf(tsDnodeDir, "%s/dnode", tsDataDir);

  if (!taosMkDir(tsDnodeDir)) {
    dError("failed to create dir:%s since %s", tsDnodeDir, strerror(errno));
    return -1;
  }

  if (!taosMkDir(tsMnodeDir)) {
    dError("failed to create dir:%s since %s", tsMnodeDir, strerror(errno));
    return -1;
  }

  if (!taosMkDir(tsVnodeDir)) {
    dError("failed to create dir:%s since %s", tsVnodeDir, strerror(errno));
    return -1;
  }

  if (dnodeCheckRunning(tsDnodeDir) != 0) {
    return -1;
  }

  return 0;
}

static int32_t dnodeInitMain() {
  tsInt.runStat = DN_RUN_STAT_STOPPED;
  tscEmbedded = 1;
  taosIgnSIGPIPE();
  taosBlockSIGPIPE();
  taosResolveCRC();
  taosInitGlobalCfg();
  taosReadGlobalLogCfg();
  taosSetCoreDump(tsEnableCoreFile);

  if (!taosMkDir(tsLogDir)) {
    printf("failed to create dir: %s, reason: %s\n", tsLogDir, strerror(errno));
    return -1;
  }

  char temp[TSDB_FILENAME_LEN];
  sprintf(temp, "%s/taosdlog", tsLogDir);
  if (taosInitLog(temp, tsNumOfLogLines, 1) < 0) {
    printf("failed to init log file\n");
  }

  if (!taosReadGlobalCfg()) {
    taosPrintGlobalCfg();
    dError("TDengine read global config failed");
    return -1;
  }

  dInfo("start to initialize TDengine");

  taosInitNotes();

  if (taosCheckGlobalCfg() != 0) {
    return -1;
  }

  dnodeInitDir();

  return -1;
}

static void dnodeCleanupMain() {
  taos_cleanup();
  taosCloseLog();
  taosStopCacheRefreshWorker();
}

int32_t dnodeInit() {
  SSteps *steps = taosStepInit(24, dnodeReportStartup);
  if (steps == NULL) return -1;

  taosStepAdd(steps, "dnode-main", dnodeInitMain, dnodeCleanupMain);
  taosStepAdd(steps, "dnode-rpc", rpcInit, rpcCleanup);
  taosStepAdd(steps, "dnode-tfs", NULL, NULL);
  taosStepAdd(steps, "dnode-wal", walInit, walCleanUp);
  taosStepAdd(steps, "dnode-sync", syncInit, syncCleanUp);
  taosStepAdd(steps, "dnode-dnode", dnodeInitDnode, dnodeCleanupDnode);
  taosStepAdd(steps, "dnode-vnodes", dnodeInitVnodes, dnodeCleanupVnodes);
  taosStepAdd(steps, "dnode-mnode", dnodeInitMnode, dnodeCleanupMnode);
  taosStepAdd(steps, "dnode-trans", dnodeInitTrans, dnodeCleanupTrans);

  tsInt.steps = steps;
  taosStepExec(tsInt.steps);

  dnodeSetRunStat(DN_RUN_STAT_RUNNING);
  dnodeReportStartupFinished("TDengine", "initialized successfully");
  dInfo("TDengine is initialized successfully");

  return 0;
}

void dnodeCleanup() {
  if (dnodeGetRunStat() != DN_RUN_STAT_STOPPED) {
    dnodeSetRunStat(DN_RUN_STAT_STOPPED);
    taosStepCleanup(tsInt.steps);
    tsInt.steps = NULL;
  }
}
