#include "AppwriteClientInstance.h"

HttpClientHelper &getAppwriteClient()
{
    static HttpClientHelper instance(
        APPWRITE_ENDPOINT,
        APPWRITE_PROJECT_ID,
        APPWRITE_API_KEY,
        15000 // timeout in milliseconds
    );
    return instance;
}