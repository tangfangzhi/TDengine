/*
 * Copyright (c) 2019 TAOS Data, Inc. <cli@taosdata.com>
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

#include <stdlib.h>
#include "cacheTable.h"
#include "cacheint.h"
#include "cacheItem.h"
#include "hash.h"

static int        cacheInitBucket(cacheTableBucket* pBucket, uint32_t id);
static cacheItem* cacheFindItemByKey(cacheTable* pTable, const char* key, uint8_t nkey, cacheItem **ppPrev);
static void       cacheRemoveTableItem(cache_t* pCache, cacheTableBucket* pBucket, cacheItem* pItem, cacheItem* pPrev);

cacheTable* cacheCreateTable(cache_t* cache, cacheTableOption* option) {
  cacheTable* pTable = calloc(1, sizeof(cacheTable));
  if (pTable == NULL) {
    goto error;
  }

  if (cacheMutexInit(&pTable->mutex) != 0) {
    goto error;
  }

  pTable->capacity = option->initNum;
  pTable->pBucket = malloc(sizeof(cacheTableBucket) * pTable->capacity);
  if (pTable->pBucket == NULL) {
    goto error;
  }
  memset(pTable->pBucket, 0, sizeof(cacheTableBucket) * pTable->capacity);
  int i = 0;
  for (i = 0; i < pTable->capacity; i++) {
    cacheInitBucket(&(pTable->pBucket[i]), i);
  }

  pTable->hashFp = taosGetDefaultHashFunction(option->keyType);
  pTable->option = *option;
  pTable->pCache = cache;

  if (cache->tableHead == NULL) {
    cache->tableHead = pTable;
    pTable->next = NULL;
  } else {
    pTable->next = cache->tableHead;
    cache->tableHead = pTable;
  }

  return pTable;

error:
  if (pTable) {
    free(pTable);
  }

  return NULL;
}

static int cacheInitBucket(cacheTableBucket* pBucket, uint32_t id) {
  pBucket->hash = id;
  pBucket->head = NULL;
  return cacheMutexInit(&(pBucket->mutex));
}

static cacheItem* cacheFindItemByKey(cacheTable* pTable, const char* key, uint8_t nkey, cacheItem **ppPrev) {
  cacheItem* pItem = NULL;
  uint32_t index = pTable->hashFp(key, nkey) % pTable->capacity;

  pItem = pTable->pBucket[index].head;
  if (ppPrev) *ppPrev = NULL;

  while (pItem != NULL) {
    if (!item_equal_key(pItem, key, nkey)) {
      if (ppPrev) {
        *ppPrev = pItem;
      }
      pItem = pItem->h_next;
      continue;
    }
    return pItem;
  }

  return NULL;
}

static void cacheRemoveTableItem(cache_t* pCache, cacheTableBucket* pBucket, cacheItem* pItem, cacheItem* pPrev) {
  if (pItem != NULL) {
    if (pPrev != NULL) {
      pPrev->h_next = pItem->h_next;
    }
    if (pBucket->head == pItem) {
      pBucket->head = pItem->h_next;
    }
    pItem->pTable = NULL;
    pItem->hash = 0;
  }
}

int cacheTablePut(cacheTable* pTable, cacheItem* pItem) {
  uint32_t index = pTable->hashFp(item_key(pItem), pItem->nkey) % pTable->capacity;
  cacheTableBucket* pBucket = &(pTable->pBucket[index]);

  cacheTableLockBucket(pTable, index);

  cacheItem *pOldItem, *pPrev;
  pOldItem = cacheFindItemByKey(pTable, item_key(pItem), pItem->nkey, &pPrev);

  cacheRemoveTableItem(pTable->pCache, pBucket, pOldItem, pPrev);
  
  pItem->h_next = pBucket->head;
  pBucket->head->h_next = pItem;
  pItem->hash = index;
  pItem->pTable = pTable;

  cacheTableUnlockBucket(pTable, index);
  return CACHE_OK;
}

cacheItem* cacheTableGet(cacheTable* pTable, const char* key, uint8_t nkey) {
  uint32_t index = pTable->hashFp(key, nkey) % pTable->capacity;

  cacheTableLockBucket(pTable, index);
  
  cacheItem *pItem;
  pItem = cacheFindItemByKey(pTable, key, nkey, NULL);

  cacheTableUnlockBucket(pTable, index);
  return pItem;
}

void cacheTableRemove(cacheTable* pTable, const char* key, uint8_t nkey) {
  uint32_t index = pTable->hashFp(key, nkey) % pTable->capacity;
  cacheTableBucket* pBucket = &(pTable->pBucket[index]);

  cacheTableLockBucket(pTable, index);
  
  cacheItem *pItem, *pPrev;
  pItem = cacheFindItemByKey(pTable, key, nkey, &pPrev);

  cacheRemoveTableItem(pTable->pCache, pBucket, pItem, pPrev);

  cacheTableUnlockBucket(pTable, index);
}