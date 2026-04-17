#include "AppwriteClientInstance.h"

AppwriteHttpClient &getAppwriteClient()
{
    static AppwriteHttpClient instance(
        APPWRITE_ENDPOINT,
        APPWRITE_PROJECT_ID,
        APPWRITE_API_KEY,
        15000 // timeout in milliseconds
    );
    return instance;
}