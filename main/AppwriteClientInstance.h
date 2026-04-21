#pragma once

#include "HttpClientHelper.h"
#include "secrets.h"

/**
 * @brief Global Appwrite HTTP client instance.
 *
 * Returns a single shared instance of HttpClientHelper configured with
 * APPWRITE_ENDPOINT, APPWRITE_PROJECT_ID, APPWRITE_API_KEY and a timeout of 90000 ms.
 *
 * Usage:
 *   HttpClientHelper& client = getAppwriteClient();
 *   client.httpGet(...);
 */
HttpClientHelper &getAppwriteClient();