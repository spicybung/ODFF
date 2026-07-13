#include "Application.h"

int main()
{
    Application application;

    if (!application.Initialize())
    {
        return 1;
    }

    const int result = application.Run();
    application.Shutdown();
    return result;
}
