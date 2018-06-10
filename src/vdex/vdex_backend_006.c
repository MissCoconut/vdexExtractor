/*

   vdexExtractor
   -----------------------------------------

   Anestis Bechtsoudis <anestis@census-labs.com>
   Copyright 2017 - 2018 by CENSUS S.A. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/

#include <sys/mman.h>

#include "vdex_backend_006.h"
#include "vdex_decompiler_006.h"
#include "../out_writer.h"
#include "../utils.h"

static inline u4 decodeUint32WithOverflowCheck(const u1 **in, const u1 *end) {
  CHECK_LT(*in, end);
  return dex_readULeb128(in);
}

static void decodeDepStrings(const u1 **in, const u1 *end, vdexDepStrings_006 *depStrings) {
  u4 numOfEntries = decodeUint32WithOverflowCheck(in, end);
  depStrings->strings = utils_calloc(numOfEntries * sizeof(char *));
  depStrings->numberOfStrings = numOfEntries;
  for (u4 i = 0; i < numOfEntries; ++i) {
    CHECK_LT(*in, end);
    const char *stringStart = (const char *)(*in);
    depStrings->strings[i] = stringStart;
    *in += strlen(stringStart) + 1;
  }
}

static void decodeDepTypeSet(const u1 **in, const u1 *end, vdexDepTypeSet_006 *pVdexDepTypeSet) {
  u4 numOfEntries = decodeUint32WithOverflowCheck(in, end);
  pVdexDepTypeSet->pVdexDepSets = utils_malloc(numOfEntries * sizeof(vdexDepSet_006));
  pVdexDepTypeSet->numberOfEntries = numOfEntries;
  for (u4 i = 0; i < numOfEntries; ++i) {
    pVdexDepTypeSet->pVdexDepSets[i].dstIndex = decodeUint32WithOverflowCheck(in, end);
    pVdexDepTypeSet->pVdexDepSets[i].srcIndex = decodeUint32WithOverflowCheck(in, end);
  }
}

static void decodeDepClasses(const u1 **in,
                             const u1 *end,
                             vdexDepClassResSet_006 *pVdexDepClassResSet) {
  u4 numOfEntries = decodeUint32WithOverflowCheck(in, end);
  pVdexDepClassResSet->pVdexDepClasses = utils_malloc(numOfEntries * sizeof(vdexDepClassRes_006));
  pVdexDepClassResSet->numberOfEntries = numOfEntries;
  for (u4 i = 0; i < numOfEntries; ++i) {
    pVdexDepClassResSet->pVdexDepClasses[i].typeIdx = decodeUint32WithOverflowCheck(in, end);
    pVdexDepClassResSet->pVdexDepClasses[i].accessFlags = decodeUint32WithOverflowCheck(in, end);
  }
}

static void decodeDepFields(const u1 **in, const u1 *end, vdexDepFieldResSet_006 *pVdexDepFieldResSet) {
  u4 numOfEntries = decodeUint32WithOverflowCheck(in, end);
  pVdexDepFieldResSet->pVdexDepFields = utils_malloc(numOfEntries * sizeof(vdexDepFieldRes_006));
  pVdexDepFieldResSet->numberOfEntries = numOfEntries;
  for (u4 i = 0; i < pVdexDepFieldResSet->numberOfEntries; ++i) {
    pVdexDepFieldResSet->pVdexDepFields[i].fieldIdx = decodeUint32WithOverflowCheck(in, end);
    pVdexDepFieldResSet->pVdexDepFields[i].accessFlags = decodeUint32WithOverflowCheck(in, end);
    pVdexDepFieldResSet->pVdexDepFields[i].declaringClassIdx =
        decodeUint32WithOverflowCheck(in, end);
  }
}

static void decodeDepMethods(const u1 **in,
                             const u1 *end,
                             vdexDepMethodResSet_006 *pVdexDepMethodResSet) {
  u4 numOfEntries = decodeUint32WithOverflowCheck(in, end);
  pVdexDepMethodResSet->pVdexDepMethods = utils_malloc(numOfEntries * sizeof(vdexDepMethodRes_006));
  pVdexDepMethodResSet->numberOfEntries = numOfEntries;
  for (u4 i = 0; i < numOfEntries; ++i) {
    pVdexDepMethodResSet->pVdexDepMethods[i].methodIdx = decodeUint32WithOverflowCheck(in, end);
    pVdexDepMethodResSet->pVdexDepMethods[i].accessFlags = decodeUint32WithOverflowCheck(in, end);
    pVdexDepMethodResSet->pVdexDepMethods[i].declaringClassIdx =
        decodeUint32WithOverflowCheck(in, end);
  }
}

static void decodeDepUnvfyClasses(const u1 **in,
                                  const u1 *end,
                                  vdexDepUnvfyClassesSet_006 *pVdexDepUnvfyClassesSet) {
  u4 numOfEntries = decodeUint32WithOverflowCheck(in, end);
  pVdexDepUnvfyClassesSet->pVdexDepUnvfyClasses =
      utils_malloc(numOfEntries * sizeof(vdexDepUnvfyClass_006));
  pVdexDepUnvfyClassesSet->numberOfEntries = numOfEntries;
  for (u4 i = 0; i < numOfEntries; ++i) {
    pVdexDepUnvfyClassesSet->pVdexDepUnvfyClasses[i].typeIdx =
        decodeUint32WithOverflowCheck(in, end);
  }
}

static const char *getStringFromId(const vdexDepData_006 *pVdexDepData,
                                   u4 stringId,
                                   const u1 *dexFileBuf) {
  const dexHeader *pDexHeader = (const dexHeader *)dexFileBuf;
  vdexDepStrings_006 extraStrings = pVdexDepData->extraStrings;
  u4 numIdsInDex = pDexHeader->stringIdsSize;
  if (stringId < numIdsInDex) {
    return dex_getStringDataByIdx(dexFileBuf, stringId);
  } else {
    // Adjust offset
    stringId -= numIdsInDex;
    CHECK_LT(stringId, extraStrings.numberOfStrings);
    return extraStrings.strings[stringId];
  }
}

static void dumpDepsMethodInfo(const u1 *dexFileBuf,
                               const vdexDepData_006 *pVdexDepData,
                               const vdexDepMethodResSet_006 *pMethods,
                               const char *kind) {
  log_dis(" %s method dependencies: number_of_methods=%" PRIu32 "\n", kind,
          pMethods->numberOfEntries);
  for (u4 i = 0; i < pMethods->numberOfEntries; ++i) {
    const dexMethodId *pDexMethodId =
        dex_getMethodId(dexFileBuf, pMethods->pVdexDepMethods[i].methodIdx);
    u2 accessFlags = pMethods->pVdexDepMethods[i].accessFlags;
    const char *methodSig = dex_getMethodSignature(dexFileBuf, pDexMethodId);
    log_dis("  %04" PRIu32 ": '%s'->'%s':'%s' is expected to be ", i,
            dex_getMethodDeclaringClassDescriptor(dexFileBuf, pDexMethodId),
            dex_getMethodName(dexFileBuf, pDexMethodId), methodSig);
    free((void *)methodSig);
    if (accessFlags == kUnresolvedMarker) {
      log_dis("unresolved\n");
    } else {
      log_dis(
          "in class '%s', have the access flags '%" PRIu16 "', and be of kind '%s'\n",
          getStringFromId(pVdexDepData, pMethods->pVdexDepMethods[i].declaringClassIdx, dexFileBuf),
          accessFlags, kind);
    }
  }
}

static vdexDeps_006 *initDepsInfo(const u1 *vdexFileBuf) {
  if (vdex_006_GetVerifierDepsDataSize(vdexFileBuf) == 0) {
    // Return eagerly, as the first thing we expect from VerifierDeps data is
    // the number of created strings, even if there is no dependency.
    return NULL;
  }

  vdexDeps_006 *pVdexDeps = utils_malloc(sizeof(vdexDeps_006));

  const vdexHeader_006 *pVdexHeader = (const vdexHeader_006 *)vdexFileBuf;
  pVdexDeps->numberOfDexFiles = pVdexHeader->numberOfDexFiles;
  pVdexDeps->pVdexDepData = utils_malloc(sizeof(vdexDepData_006) * pVdexDeps->numberOfDexFiles);

  const u1 *dexFileBuf = NULL;
  u4 offset = 0;

  const u1 *depsDataStart = vdex_006_GetVerifierDepsData(vdexFileBuf);
  const u1 *depsDataEnd = depsDataStart + vdex_006_GetVerifierDepsDataSize(vdexFileBuf);

  for (u4 i = 0; i < pVdexDeps->numberOfDexFiles; ++i) {
    dexFileBuf = vdex_006_GetNextDexFileData(vdexFileBuf, &offset);
    if (dexFileBuf == NULL) {
      LOGMSG(l_FATAL, "Failed to extract Dex file buffer from loaded Vdex");
    }

    // Process encoded extra strings
    decodeDepStrings(&depsDataStart, depsDataEnd, &pVdexDeps->pVdexDepData[i].extraStrings);

    // Process encoded assignable types
    decodeDepTypeSet(&depsDataStart, depsDataEnd, &pVdexDeps->pVdexDepData[i].assignTypeSets);

    // Process encoded unassignable types
    decodeDepTypeSet(&depsDataStart, depsDataEnd, &pVdexDeps->pVdexDepData[i].unassignTypeSets);

    // Process encoded classes
    decodeDepClasses(&depsDataStart, depsDataEnd, &pVdexDeps->pVdexDepData[i].classes);

    // Process encoded fields
    decodeDepFields(&depsDataStart, depsDataEnd, &pVdexDeps->pVdexDepData[i].fields);

    // Process encoded direct_methods
    decodeDepMethods(&depsDataStart, depsDataEnd, &pVdexDeps->pVdexDepData[i].directMethods);

    // Process encoded virtual_methods
    decodeDepMethods(&depsDataStart, depsDataEnd, &pVdexDeps->pVdexDepData[i].virtualMethods);

    // Process encoded interface_methods
    decodeDepMethods(&depsDataStart, depsDataEnd, &pVdexDeps->pVdexDepData[i].interfaceMethods);

    // Process encoded unverified classes
    decodeDepUnvfyClasses(&depsDataStart, depsDataEnd, &pVdexDeps->pVdexDepData[i].unvfyClasses);
  }
  CHECK_LE(depsDataStart, depsDataEnd);
  return pVdexDeps;
}

static void destroyDepsInfo(const vdexDeps_006 *pVdexDeps) {
  for (u4 i = 0; i < pVdexDeps->numberOfDexFiles; ++i) {
    free((void *)pVdexDeps->pVdexDepData[i].extraStrings.strings);
    free((void *)pVdexDeps->pVdexDepData[i].assignTypeSets.pVdexDepSets);
    free((void *)pVdexDeps->pVdexDepData[i].unassignTypeSets.pVdexDepSets);
    free((void *)pVdexDeps->pVdexDepData[i].classes.pVdexDepClasses);
    free((void *)pVdexDeps->pVdexDepData[i].fields.pVdexDepFields);
    free((void *)pVdexDeps->pVdexDepData[i].directMethods.pVdexDepMethods);
    free((void *)pVdexDeps->pVdexDepData[i].virtualMethods.pVdexDepMethods);
    free((void *)pVdexDeps->pVdexDepData[i].interfaceMethods.pVdexDepMethods);
    free((void *)pVdexDeps->pVdexDepData[i].unvfyClasses.pVdexDepUnvfyClasses);
  }
  free((void *)pVdexDeps->pVdexDepData);
  free((void *)pVdexDeps);
}

void vdex_backend_006_dumpDepsInfo(const u1 *vdexFileBuf) {
  // Initialize depsInfo structs
  vdexDeps_006 *pVdexDeps = initDepsInfo(vdexFileBuf);
  if (pVdexDeps == NULL) {
    LOGMSG(l_WARN, "Empty verified dependency data");
    return;
  }

  log_dis("------- Vdex Deps Info -------\n");

  const u1 *dexFileBuf = NULL;
  u4 offset = 0;
  for (u4 i = 0; i < pVdexDeps->numberOfDexFiles; ++i) {
    const vdexDepData_006 *pVdexDepData = &pVdexDeps->pVdexDepData[i];
    log_dis("dex file #%" PRIu32 "\n", i);
    dexFileBuf = vdex_006_GetNextDexFileData(vdexFileBuf, &offset);
    if (dexFileBuf == NULL) {
      LOGMSG(l_FATAL, "Failed to extract Dex file buffer from loaded Vdex");
    }

    vdexDepStrings_006 strings = pVdexDepData->extraStrings;
    log_dis(" extra strings: number_of_strings=%" PRIu32 "\n", strings.numberOfStrings);
    for (u4 i = 0; i < strings.numberOfStrings; ++i) {
      log_dis("  %04" PRIu32 ": '%s'\n", i, strings.strings[i]);
    }

    vdexDepTypeSet_006 aTypes = pVdexDepData->assignTypeSets;
    log_dis(" assignable type sets: number_of_sets=%" PRIu32 "\n", aTypes.numberOfEntries);
    for (u4 i = 0; i < aTypes.numberOfEntries; ++i) {
      log_dis("  %04" PRIu32 ": '%s' must be assignable to '%s'\n", i,
              getStringFromId(pVdexDepData, aTypes.pVdexDepSets[i].srcIndex, dexFileBuf),
              getStringFromId(pVdexDepData, aTypes.pVdexDepSets[i].dstIndex, dexFileBuf));
    }

    vdexDepTypeSet_006 unTypes = pVdexDepData->unassignTypeSets;
    log_dis(" unassignable type sets: number_of_sets=%" PRIu32 "\n", unTypes.numberOfEntries);
    for (u4 i = 0; i < unTypes.numberOfEntries; ++i) {
      log_dis("  %04" PRIu32 ": '%s' must not be assignable to '%s'\n", i,
              getStringFromId(pVdexDepData, unTypes.pVdexDepSets[i].srcIndex, dexFileBuf),
              getStringFromId(pVdexDepData, unTypes.pVdexDepSets[i].dstIndex, dexFileBuf));
    }

    log_dis(" class dependencies: number_of_classes=%" PRIu32 "\n",
            pVdexDepData->classes.numberOfEntries);
    for (u4 i = 0; i < pVdexDepData->classes.numberOfEntries; ++i) {
      u2 accessFlags = pVdexDepData->classes.pVdexDepClasses[i].accessFlags;
      log_dis("  %04" PRIu32 ": '%s' '%s' be resolved with access flags '%" PRIu16 "'\n", i,
              dex_getStringByTypeIdx(dexFileBuf, pVdexDepData->classes.pVdexDepClasses[i].typeIdx),
              accessFlags == kUnresolvedMarker ? "must not" : "must", accessFlags);
    }

    log_dis(" field dependencies: number_of_fields=%" PRIu32 "\n",
            pVdexDepData->fields.numberOfEntries);
    for (u4 i = 0; i < pVdexDepData->fields.numberOfEntries; ++i) {
      vdexDepFieldRes_006 fieldRes = pVdexDepData->fields.pVdexDepFields[i];
      const dexFieldId *pDexFieldId = dex_getFieldId(dexFileBuf, fieldRes.fieldIdx);
      log_dis("  %04" PRIu32 ": '%s'->'%s':'%s' is expected to be ", i,
              dex_getFieldDeclaringClassDescriptor(dexFileBuf, pDexFieldId),
              dex_getFieldName(dexFileBuf, pDexFieldId),
              dex_getFieldTypeDescriptor(dexFileBuf, pDexFieldId));
      if (fieldRes.accessFlags == kUnresolvedMarker) {
        log_dis("unresolved\n");
      } else {
        log_dis("in class '%s' and have the access flags '%" PRIu16 "'\n",
                getStringFromId(pVdexDepData, fieldRes.declaringClassIdx, dexFileBuf),
                fieldRes.accessFlags);
      }
    }

    dumpDepsMethodInfo(dexFileBuf, pVdexDepData, &pVdexDepData->directMethods, "direct");
    dumpDepsMethodInfo(dexFileBuf, pVdexDepData, &pVdexDepData->virtualMethods, "virtual");
    dumpDepsMethodInfo(dexFileBuf, pVdexDepData, &pVdexDepData->interfaceMethods, "interface");

    log_dis(" unverified classes: number_of_classes=%" PRIu32 "\n",
            pVdexDepData->unvfyClasses.numberOfEntries);
    for (u4 i = 0; i < pVdexDepData->unvfyClasses.numberOfEntries; ++i) {
      log_dis("  %04" PRIu32 ": '%s' is expected to be verified at runtime\n", i,
              dex_getStringByTypeIdx(dexFileBuf,
                                     pVdexDepData->unvfyClasses.pVdexDepUnvfyClasses[i].typeIdx));
    }
  }
  log_dis("----- EOF Vdex Deps Info -----\n");

  // Cleanup
  destroyDepsInfo(pVdexDeps);
}

int vdex_backend_006_process(const char *VdexFileName, const u1 *cursor, const runArgs_t *pRunArgs) {

  const vdexHeader_006 *pVdexHeader = (const vdexHeader_006 *)cursor;
  const u1 *quickening_info_ptr = vdex_006_GetQuickeningInfo(cursor);
  const u1 *const quickening_info_end =
      vdex_006_GetQuickeningInfo(cursor) + vdex_006_GetQuickeningInfoSize(cursor);

  const u1 *dexFileBuf = NULL;
  u4 offset = 0;

  // For each Dex file
  for (size_t dex_file_idx = 0; dex_file_idx < pVdexHeader->numberOfDexFiles; ++dex_file_idx) {
    dexFileBuf = vdex_006_GetNextDexFileData(cursor, &offset);
    if (dexFileBuf == NULL) {
      LOGMSG(l_ERROR, "Failed to extract 'classes%zu.dex' - skipping", dex_file_idx);
      continue;
    }

    const dexHeader *pDexHeader = (const dexHeader *)dexFileBuf;

    // Check if valid Dex file
    dex_dumpHeaderInfo(pDexHeader);
    if (!dex_isValidDexMagic(pDexHeader)) {
      LOGMSG(l_ERROR, "'classes%zu.dex' is an invalid Dex file - skipping", dex_file_idx);
      continue;
    }

    // For each class
    log_dis("file #%zu: classDefsSize=%" PRIu32 "\n", dex_file_idx, pDexHeader->classDefsSize);
    for (u4 i = 0; i < pDexHeader->classDefsSize; ++i) {
      const dexClassDef *pDexClassDef = dex_getClassDef(dexFileBuf, i);
      dex_dumpClassInfo(dexFileBuf, i);

      // Cursor for currently processed class data item
      const u1 *curClassDataCursor;
      if (pDexClassDef->classDataOff == 0) {
        continue;
      } else {
        curClassDataCursor = dexFileBuf + pDexClassDef->classDataOff;
      }

      dexClassDataHeader pDexClassDataHeader;
      memset(&pDexClassDataHeader, 0, sizeof(dexClassDataHeader));
      dex_readClassDataHeader(&curClassDataCursor, &pDexClassDataHeader);

      // Skip static fields
      for (u4 j = 0; j < pDexClassDataHeader.staticFieldsSize; ++j) {
        dexField pDexField;
        memset(&pDexField, 0, sizeof(dexField));
        dex_readClassDataField(&curClassDataCursor, &pDexField);
      }

      // Skip instance fields
      for (u4 j = 0; j < pDexClassDataHeader.instanceFieldsSize; ++j) {
        dexField pDexField;
        memset(&pDexField, 0, sizeof(dexField));
        dex_readClassDataField(&curClassDataCursor, &pDexField);
      }

      // For each direct method
      for (u4 j = 0; j < pDexClassDataHeader.directMethodsSize; ++j) {
        dexMethod curDexMethod;
        memset(&curDexMethod, 0, sizeof(dexMethod));
        dex_readClassDataMethod(&curClassDataCursor, &curDexMethod);
        dex_dumpMethodInfo(dexFileBuf, &curDexMethod, j, "direct");

        // Skip empty methods
        if (curDexMethod.codeOff == 0) {
          continue;
        }

        if (pRunArgs->unquicken && vdex_006_GetQuickeningInfoSize(cursor) != 0) {
          // For quickening info blob the first 4bytes are the inner blobs size
          u4 quickening_size = *(u4 *)quickening_info_ptr;
          quickening_info_ptr += sizeof(u4);
          if (!vdex_decompiler_006_decompile(dexFileBuf, &curDexMethod, quickening_info_ptr,
                                         quickening_size, true)) {
            LOGMSG(l_ERROR, "Failed to decompile Dex file");
            return -1;
          }
          quickening_info_ptr += quickening_size;
        } else {
          vdex_decompiler_006_walk(dexFileBuf, &curDexMethod);
        }
      }

      // For each virtual method
      for (u4 j = 0; j < pDexClassDataHeader.virtualMethodsSize; ++j) {
        dexMethod curDexMethod;
        memset(&curDexMethod, 0, sizeof(dexMethod));
        dex_readClassDataMethod(&curClassDataCursor, &curDexMethod);
        dex_dumpMethodInfo(dexFileBuf, &curDexMethod, j, "virtual");

        // Skip native or abstract methods
        if (curDexMethod.codeOff == 0) {
          continue;
        }

        if (pRunArgs->unquicken && vdex_006_GetQuickeningInfoSize(cursor) != 0) {
          // For quickening info blob the first 4bytes are the inner blobs size
          u4 quickening_size = *(u4 *)quickening_info_ptr;
          quickening_info_ptr += sizeof(u4);
          if (!vdex_decompiler_006_decompile(dexFileBuf, &curDexMethod, quickening_info_ptr,
                                         quickening_size, true)) {
            LOGMSG(l_ERROR, "Failed to decompile Dex file");
            return -1;
          }
          quickening_info_ptr += quickening_size;
        } else {
          vdex_decompiler_006_walk(dexFileBuf, &curDexMethod);
        }
      }
    }

    if (pRunArgs->unquicken) {
      // If unquicken was successful original checksum should verify
      u4 curChecksum = dex_computeDexCRC(dexFileBuf, pDexHeader->fileSize);
      if (curChecksum != pDexHeader->checksum) {
        // If ignore CRC errors is enabled, repair CRC (see issue #3)
        if (pRunArgs->ignoreCrc) {
          dex_repairDexCRC(dexFileBuf, pDexHeader->fileSize);
        } else {
          LOGMSG(l_ERROR,
                 "Unexpected checksum (%" PRIx32 " vs %" PRIx32 ") - failed to unquicken Dex file",
                 curChecksum, pDexHeader->checksum);
          return -1;
        }
      }
    } else {
      // Repair CRC if not decompiling so we can still run Dex parsing tools against output
      dex_repairDexCRC(dexFileBuf, pDexHeader->fileSize);
    }

    if (!outWriter_DexFile(pRunArgs, VdexFileName, dex_file_idx, dexFileBuf,
                           pDexHeader->fileSize)) {
      return -1;
    }
  }

  if (pRunArgs->unquicken && (quickening_info_ptr != quickening_info_end)) {
    LOGMSG(l_ERROR, "Failed to process all quickening info data");
    return -1;
  }

  return pVdexHeader->numberOfDexFiles;
}