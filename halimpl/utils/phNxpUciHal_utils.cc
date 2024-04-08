/*
 * Copyright 2012-2020, 2023 NXP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <errno.h>
#include <pthread.h>
#include <log/log.h>

#include <phNxpLog.h>
#include <phNxpUciHal.h>
#include <phNxpUciHal_utils.h>

using namespace std;
map<uint16_t, vector<uint16_t>> input_map;
map<uint16_t, vector<uint16_t>> conf_map;

/*********************** Link list functions **********************************/

/*******************************************************************************
**
** Function         listInit
**
** Description      List initialization
**
** Returns          1, if list initialized, 0 otherwise
**
*******************************************************************************/
int listInit(struct listHead* pList) {
  pList->pFirst = NULL;
  if (pthread_mutex_init(&pList->mutex, NULL) == -1) {
    NXPLOG_UCIHAL_E("Mutex creation failed (errno=0x%08x)", errno);
    return 0;
  }

  return 1;
}

/*******************************************************************************
**
** Function         listDestroy
**
** Description      List destruction
**
** Returns          1, if list destroyed, 0 if failed
**
*******************************************************************************/
int listDestroy(struct listHead* pList) {
  int bListNotEmpty = 1;
  while (bListNotEmpty) {
    bListNotEmpty = listGetAndRemoveNext(pList, NULL);
  }

  if (pthread_mutex_destroy(&pList->mutex) == -1) {
    NXPLOG_UCIHAL_E("Mutex destruction failed (errno=0x%08x)", errno);
    return 0;
  }

  return 1;
}

/*******************************************************************************
**
** Function         listAdd
**
** Description      Add a node to the list
**
** Returns          1, if added, 0 if otherwise
**
*******************************************************************************/
int listAdd(struct listHead* pList, void* pData) {
  struct listNode* pNode;
  struct listNode* pLastNode;
  int result;

  /* Create node */
  pNode = (struct listNode*)malloc(sizeof(struct listNode));
  if (pNode == NULL) {
    result = 0;
    NXPLOG_UCIHAL_E("Failed to malloc");
    goto clean_and_return;
  }
  pNode->pData = pData;
  pNode->pNext = NULL;
  pthread_mutex_lock(&pList->mutex);

  /* Add the node to the list */
  if (pList->pFirst == NULL) {
    /* Set the node as the head */
    pList->pFirst = pNode;
  } else {
    /* Seek to the end of the list */
    pLastNode = pList->pFirst;
    while (pLastNode->pNext != NULL) {
      pLastNode = pLastNode->pNext;
    }

    /* Add the node to the current list */
    pLastNode->pNext = pNode;
  }

  result = 1;

clean_and_return:
  pthread_mutex_unlock(&pList->mutex);
  return result;
}

/*******************************************************************************
**
** Function         listRemove
**
** Description      Remove node from the list
**
** Returns          1, if removed, 0 if otherwise
**
*******************************************************************************/
int listRemove(struct listHead* pList, void* pData) {
  struct listNode* pNode;
  struct listNode* pRemovedNode;
  int result;

  pthread_mutex_lock(&pList->mutex);

  if (pList->pFirst == NULL) {
    /* Empty list */
    NXPLOG_UCIHAL_E("Failed to deallocate (list empty)");
    result = 0;
    goto clean_and_return;
  }

  pNode = pList->pFirst;
  if (pList->pFirst->pData == pData) {
    /* Get the removed node */
    pRemovedNode = pNode;

    /* Remove the first node */
    pList->pFirst = pList->pFirst->pNext;
  } else {
    while (pNode->pNext != NULL) {
      if (pNode->pNext->pData == pData) {
        /* Node found ! */
        break;
      }
      pNode = pNode->pNext;
    }

    if (pNode->pNext == NULL) {
      /* Node not found */
      result = 0;
      NXPLOG_UCIHAL_E("Failed to deallocate (not found %8p)", pData);
      goto clean_and_return;
    }

    /* Get the removed node */
    pRemovedNode = pNode->pNext;

    /* Remove the node from the list */
    pNode->pNext = pNode->pNext->pNext;
  }

  /* Deallocate the node */
  free(pRemovedNode);

  result = 1;

clean_and_return:
  pthread_mutex_unlock(&pList->mutex);
  return result;
}

/*******************************************************************************
**
** Function         listGetAndRemoveNext
**
** Description      Get next node on the list and remove it
**
** Returns          1, if successful, 0 if otherwise
**
*******************************************************************************/
int listGetAndRemoveNext(struct listHead* pList, void** ppData) {
  struct listNode* pNode;
  int result;

  pthread_mutex_lock(&pList->mutex);

  if (pList->pFirst == NULL) {
    /* Empty list */
    NXPLOG_UCIHAL_D("Failed to deallocate (list empty)");
    result = 0;
    goto clean_and_return;
  }

  /* Work on the first node */
  pNode = pList->pFirst;

  /* Return the data */
  if (ppData != NULL) {
    *ppData = pNode->pData;
  }

  /* Remove and deallocate the node */
  pList->pFirst = pNode->pNext;
  free(pNode);

  result = 1;

clean_and_return:
  listDump(pList);
  pthread_mutex_unlock(&pList->mutex);
  return result;
}

/*******************************************************************************
**
** Function         listDump
**
** Description      Dump list information
**
** Returns          None
**
*******************************************************************************/
void listDump(struct listHead* pList) {
  struct listNode* pNode = pList->pFirst;

  NXPLOG_UCIHAL_D("Node dump:");
  while (pNode != NULL) {
    NXPLOG_UCIHAL_D("- %8p (%8p)", pNode, pNode->pData);
    pNode = pNode->pNext;
  }

  return;
}

/* END Linked list source code */

/****************** Semaphore and mutex helper functions **********************/

static phNxpUciHal_Monitor_t* nxpucihal_monitor = NULL;

/*******************************************************************************
**
** Function         phNxpUciHal_init_monitor
**
** Description      Initialize the semaphore monitor
**
** Returns          Pointer to monitor, otherwise NULL if failed
**
*******************************************************************************/
phNxpUciHal_Monitor_t* phNxpUciHal_init_monitor(void) {
  NXPLOG_UCIHAL_D("Entering phNxpUciHal_init_monitor");

  if (nxpucihal_monitor == NULL) {
    nxpucihal_monitor =
        (phNxpUciHal_Monitor_t*)malloc(sizeof(phNxpUciHal_Monitor_t));
  }

  if (nxpucihal_monitor != NULL) {
    memset(nxpucihal_monitor, 0x00, sizeof(phNxpUciHal_Monitor_t));

    if (pthread_mutex_init(&nxpucihal_monitor->reentrance_mutex, NULL) == -1) {
      NXPLOG_UCIHAL_E("reentrance_mutex creation returned 0x%08x", errno);
      goto clean_and_return;
    }

    if (pthread_mutex_init(&nxpucihal_monitor->concurrency_mutex, NULL) == -1) {
      NXPLOG_UCIHAL_E("concurrency_mutex creation returned 0x%08x", errno);
      pthread_mutex_destroy(&nxpucihal_monitor->reentrance_mutex);
      goto clean_and_return;
    }

    if (listInit(&nxpucihal_monitor->sem_list) != 1) {
      NXPLOG_UCIHAL_E("Semaphore List creation failed");
      pthread_mutex_destroy(&nxpucihal_monitor->concurrency_mutex);
      pthread_mutex_destroy(&nxpucihal_monitor->reentrance_mutex);
      goto clean_and_return;
    }
  } else {
    NXPLOG_UCIHAL_E("nxphal_monitor creation failed");
    goto clean_and_return;
  }

  NXPLOG_UCIHAL_D("Returning with SUCCESS");

  return nxpucihal_monitor;

clean_and_return:
  NXPLOG_UCIHAL_D("Returning with FAILURE");

  if (nxpucihal_monitor != NULL) {
    free(nxpucihal_monitor);
    nxpucihal_monitor = NULL;
  }

  return NULL;
}

/*******************************************************************************
**
** Function         phNxpUciHal_cleanup_monitor
**
** Description      Clean up semaphore monitor
**
** Returns          None
**
*******************************************************************************/
void phNxpUciHal_cleanup_monitor(void) {
  if (nxpucihal_monitor != NULL) {
    pthread_mutex_destroy(&nxpucihal_monitor->concurrency_mutex);
    REENTRANCE_UNLOCK();
    pthread_mutex_destroy(&nxpucihal_monitor->reentrance_mutex);
    phNxpUciHal_releaseall_cb_data();
    listDestroy(&nxpucihal_monitor->sem_list);
    free(nxpucihal_monitor);
    nxpucihal_monitor = NULL;
  }

  return;
}

/*******************************************************************************
**
** Function         phNxpUciHal_get_monitor
**
** Description      Get monitor
**
** Returns          Pointer to monitor
**
*******************************************************************************/
phNxpUciHal_Monitor_t* phNxpUciHal_get_monitor(void) {
  if (nxpucihal_monitor == NULL) {
    NXPLOG_UCIHAL_E("nxpucihal_monitor is null");
  }
  return nxpucihal_monitor;
}

/* Initialize the callback data */
tHAL_UWB_STATUS phNxpUciHal_init_cb_data(phNxpUciHal_Sem_t* pCallbackData,
                                   void* pContext) {
  /* Create semaphore */
  if (sem_init(&pCallbackData->sem, 0, 0) == -1) {
    NXPLOG_UCIHAL_E("Semaphore creation failed");
    return UWBSTATUS_FAILED;
  }

  /* Set default status value */
  pCallbackData->status = UWBSTATUS_FAILED;

  /* Copy the context */
  pCallbackData->pContext = pContext;

  /* Add to active semaphore list */
  if (listAdd(&phNxpUciHal_get_monitor()->sem_list, pCallbackData) != 1) {
    NXPLOG_UCIHAL_E("Failed to add the semaphore to the list");
  }

  return UWBSTATUS_SUCCESS;
}

/*******************************************************************************
**
** Function         phNxpUciHal_cleanup_cb_data
**
** Description      Clean up callback data
**
** Returns          None
**
*******************************************************************************/
void phNxpUciHal_cleanup_cb_data(phNxpUciHal_Sem_t* pCallbackData) {
  /* Destroy semaphore */
  if (sem_destroy(&pCallbackData->sem)) {
    NXPLOG_UCIHAL_E(
        "phNxpUciHal_cleanup_cb_data: Failed to destroy semaphore");
  }

  /* Remove from active semaphore list */
  if (listRemove(&phNxpUciHal_get_monitor()->sem_list, pCallbackData) != 1) {
    NXPLOG_UCIHAL_E(
        "phNxpUciHal_cleanup_cb_data: Failed to remove semaphore from the "
        "list");
  }

  return;
}

int phNxpUciHal_sem_timed_wait_msec(phNxpUciHal_Sem_t* pCallbackData, long msec)
{
  int ret;
  struct timespec absTimeout;
  if (clock_gettime(CLOCK_MONOTONIC, &absTimeout) == -1) {
    NXPLOG_UCIHAL_E("clock_gettime failed");
    return -1;
  }

  if (msec > 1000L) {
    absTimeout.tv_sec += msec / 1000L;
    msec = msec % 1000L;
  }
  absTimeout.tv_nsec += msec * 1000000L;
  if (absTimeout.tv_nsec > 1000000000L) {
    absTimeout.tv_nsec -= 1000000000L;
    absTimeout.tv_sec += 1;
  }

  while ((ret = sem_timedwait_monotonic_np(&pCallbackData->sem, &absTimeout)) == -1 && errno == EINTR) {
    continue;
  }
  if (ret == -1 && errno == ETIMEDOUT) {
    pCallbackData->status = UWBSTATUS_RESPONSE_TIMEOUT;
    NXPLOG_UCIHAL_E("wait semaphore timed out");
    return -1;
  }
  return 0;
}

/*******************************************************************************
**
** Function         phNxpUciHal_releaseall_cb_data
**
** Description      Release all callback data
**
** Returns          None
**
*******************************************************************************/
void phNxpUciHal_releaseall_cb_data(void) {
  phNxpUciHal_Sem_t* pCallbackData;

  while (listGetAndRemoveNext(&phNxpUciHal_get_monitor()->sem_list,
                              (void**)&pCallbackData)) {
    pCallbackData->status = UWBSTATUS_FAILED;
    sem_post(&pCallbackData->sem);
  }

  return;
}

/* END Semaphore and mutex helper functions */

/**************************** Other functions *********************************/

/*******************************************************************************
**
** Function         phNxpUciHal_print_packet
**
** Description      Print packet
**
** Returns          None
**
*******************************************************************************/
void phNxpUciHal_print_packet(enum phNxpUciHal_Pkt_Type what, const uint8_t* p_data,
                              uint16_t len) {
  uint32_t i;
  char print_buffer[len * 3 + 1];

  if ((gLog_level.ucix_log_level >= NXPLOG_LOG_DEBUG_LOGLEVEL)) {
    /* OK to print */
  }
  else
  {
    /* Nothing to print...
     * Why prepare buffer without printing?
     */
    return;
  }

  memset(print_buffer, 0, sizeof(print_buffer));
  for (i = 0; i < len; i++) {
    snprintf(&print_buffer[i * 2], 3, "%02X", p_data[i]);
  }
  switch(what) {
    case NXP_TML_UCI_CMD_AP_2_UWBS:
    {
      NXPLOG_UCIX_D("len = %3d > %s", len, print_buffer);
    }
    break;
    case NXP_TML_UCI_RSP_NTF_UWBS_2_AP:
    {
      NXPLOG_UCIR_D("len = %3d < %s", len, print_buffer);
    }
    break;
    case NXP_TML_FW_DNLD_CMD_AP_2_UWBS:
    {
      // TODO: Should be NXPLOG_FWDNLD_D
      NXPLOG_UCIX_D("len = %3d > (FW)%s", len, print_buffer);
    }
    break;
    case NXP_TML_FW_DNLD_RSP_UWBS_2_AP:
    {
      // TODO: Should be NXPLOG_FWDNLD_D
      NXPLOG_UCIR_D("len = %3d < (FW)%s", len, print_buffer);
    }
    break;
  }

  return;
}

/*******************************************************************************
**
** Function         phNxpUciHal_emergency_recovery
**
** Description      Emergency recovery in case of no other way out
**
** Returns          None
**
*******************************************************************************/

void phNxpUciHal_emergency_recovery(void) {
  NXPLOG_UCIHAL_E("%s: abort()", __func__);
  abort();
}

/*******************************************************************************
**
** Function         phNxpUciHal_byteArrayToDouble
**
** Description      convert byte array to double
**
** Returns          double
**
*******************************************************************************/
double phNxpUciHal_byteArrayToDouble(const uint8_t* p_data) {
  double d;
  int size_d = sizeof(d);
  uint8_t ptr[size_d],ptr_1[size_d];
  memcpy(&ptr, p_data, size_d);
  for(int i=0;i<size_d;i++) {
    ptr_1[i] = ptr[size_d - 1 - i];
  }
  memcpy(&d, &ptr_1, sizeof(d));
  return d;                                                       \
}

std::map<uint16_t, std::vector<uint8_t>>
decodeTlvBytes(const std::vector<uint8_t> &ext_ids, const uint8_t *tlv_bytes, size_t tlv_len)
{
  std::map<uint16_t, std::vector<uint8_t>> ret;

  size_t i = 0;
  while ((i + 1) < tlv_len) {
    uint16_t tag;
    uint8_t len;

    uint8_t byte0 = tlv_bytes[i++];
    uint8_t byte1 = tlv_bytes[i++];
    if (std::find(ext_ids.begin(), ext_ids.end(), byte0) != ext_ids.end()) {
      if (i >= tlv_len) {
        NXPLOG_UCIHAL_E("Failed to decode TLV bytes (offset=%zu).", i);
        break;
      }
      tag = (byte0 << 8) | byte1; // 2 bytes tag as big endiann
      len = tlv_bytes[i++];
    } else {
      tag = byte0;
      len = byte1;
    }
    if ((i + len) > tlv_len) {
      NXPLOG_UCIHAL_E("Failed to decode TLV bytes (offset=%zu).", i);
      break;
    }
    ret[tag] = std::vector(&tlv_bytes[i], &tlv_bytes[i + len]);
    i += len;
  }

  return ret;
}

std::vector<uint8_t> encodeTlvBytes(const std::map<uint16_t, std::vector<uint8_t>> &tlvs)
{
  std::vector<uint8_t> bytes;

  for (auto const & [tag, val] : tlvs) {
    // Tag
    if (tag > 0xff) {
      bytes.push_back(tag >> 8);
    }
    bytes.push_back(tag & 0xff);

    // Length
    bytes.push_back(val.size());

    // Value
    bytes.insert(bytes.end(), val.begin(), val.end());
  }

  return bytes;
}
