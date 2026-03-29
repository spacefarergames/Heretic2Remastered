//
// cd_detect.h -- Heretic II CD detection and PAK file extraction.
//
// Copyright 2025
//

#pragma once

#include "q_Typedef.h"

// Detect the Heretic II CD in any CD/DVD drive.
// If found, extracts Htic2-0.pak and Htic2-1.pak from Setup/zip/h2.zip into base_dir (if not already present).
// Returns true if the CD was detected (regardless of whether extraction was needed).
qboolean CD_DetectAndExtract(const char* base_dir);

// Returns the detected CD drive path (e.g., "D:\"), or NULL if no CD was detected.
const char* CD_GetDrivePath(void);
