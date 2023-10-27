#include <unistd.h>
#include <stdio.h>

int main()
{
    uid_t uid = getuid();
    uid_t euid = geteuid();

    printf("userid is: %d, effective userid is: %d.\n", uid, euid);
    return 0;
}

// The generated executable file is named "diff_uid_euid".

// sudo chown root:root diff_uid_euid      # Change the owner of the target file to root.
// sudo chmod u+s diff_uid_euid            # Set the set-user-id flag of the target file.