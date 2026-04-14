#pragma once

#include "AppwriteHttpClient.h"
#include "secrets.h"

/**
 * @brief Global Appwrite HTTP client instance.
 *
 * Returns a single shared instance of AppwriteHttpClient configured with
 * APPWRITE_ENDPOINT, APPWRITE_PROJECT_ID, APPWRITE_API_KEY and a timeout of 90000 ms.
 *
 * Usage:
 *   AppwriteHttpClient& client = getAppwriteClient();
 *   client.httpGet(...);
 */
AppwriteHttpClient& getAppwriteClient();