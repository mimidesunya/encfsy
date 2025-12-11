using System;
using System.Runtime.InteropServices;
using System.Security;
using System.Text;

namespace EncFSy_gui
{
    /// <summary>
    /// Windows Credential Manager wrapper for secure password storage.
    /// </summary>
    public static class CredentialManager
    {
        private const string CredentialPrefix = "EncFSy:";

        #region Native Methods

        [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern bool CredWriteW(ref CREDENTIAL credential, uint flags);

        [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern bool CredReadW(string targetName, uint type, uint flags, out IntPtr credential);

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool CredDeleteW(string targetName, uint type, uint flags);

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern void CredFree(IntPtr credential);

        private const uint CRED_TYPE_GENERIC = 1;
        private const uint CRED_PERSIST_LOCAL_MACHINE = 2;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct CREDENTIAL
        {
            public uint Flags;
            public uint Type;
            public string TargetName;
            public string Comment;
            public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
            public uint CredentialBlobSize;
            public IntPtr CredentialBlob;
            public uint Persist;
            public uint AttributeCount;
            public IntPtr Attributes;
            public string TargetAlias;
            public string UserName;
        }

        #endregion

        /// <summary>
        /// Generates a credential target name from a root directory path.
        /// </summary>
        /// <param name="rootDirectory">The encrypted directory path</param>
        /// <returns>A unique target name for the credential</returns>
        private static string GetTargetName(string rootDirectory)
        {
            // Normalize the path and create a consistent target name
            string normalized = rootDirectory.TrimEnd('\\').ToLowerInvariant();
            return CredentialPrefix + normalized;
        }

        /// <summary>
        /// Saves a password to the Windows Credential Manager.
        /// </summary>
        /// <param name="rootDirectory">The encrypted directory path (used as identifier)</param>
        /// <param name="password">The password to store</param>
        /// <returns>True if successful, false otherwise</returns>
        public static bool SavePassword(string rootDirectory, SecureString password)
        {
            if (string.IsNullOrEmpty(rootDirectory) || password == null || password.Length == 0)
                return false;

            string targetName = GetTargetName(rootDirectory);
            IntPtr passwordPtr = IntPtr.Zero;

            try
            {
                // Convert SecureString to byte array
                passwordPtr = Marshal.SecureStringToGlobalAllocUnicode(password);
                byte[] passwordBytes = Encoding.Unicode.GetBytes(Marshal.PtrToStringUni(passwordPtr));

                // Allocate unmanaged memory for the password
                IntPtr credBlobPtr = Marshal.AllocHGlobal(passwordBytes.Length);
                try
                {
                    Marshal.Copy(passwordBytes, 0, credBlobPtr, passwordBytes.Length);

                    // Clear the managed byte array
                    Array.Clear(passwordBytes, 0, passwordBytes.Length);

                    var credential = new CREDENTIAL
                    {
                        Type = CRED_TYPE_GENERIC,
                        TargetName = targetName,
                        CredentialBlobSize = (uint)passwordBytes.Length,
                        CredentialBlob = credBlobPtr,
                        Persist = CRED_PERSIST_LOCAL_MACHINE,
                        UserName = Environment.UserName,
                        Comment = "EncFSy volume password"
                    };

                    return CredWriteW(ref credential, 0);
                }
                finally
                {
                    // Securely clear and free unmanaged memory
                    if (credBlobPtr != IntPtr.Zero)
                    {
                        // Zero out the memory before freeing
                        for (int i = 0; i < passwordBytes.Length; i++)
                        {
                            Marshal.WriteByte(credBlobPtr, i, 0);
                        }
                        Marshal.FreeHGlobal(credBlobPtr);
                    }
                }
            }
            finally
            {
                if (passwordPtr != IntPtr.Zero)
                    Marshal.ZeroFreeGlobalAllocUnicode(passwordPtr);
            }
        }

        /// <summary>
        /// Retrieves a password from the Windows Credential Manager.
        /// </summary>
        /// <param name="rootDirectory">The encrypted directory path (used as identifier)</param>
        /// <returns>The password as a SecureString, or null if not found</returns>
        public static SecureString GetPassword(string rootDirectory)
        {
            if (string.IsNullOrEmpty(rootDirectory))
                return null;

            string targetName = GetTargetName(rootDirectory);
            IntPtr credPtr = IntPtr.Zero;

            try
            {
                if (!CredReadW(targetName, CRED_TYPE_GENERIC, 0, out credPtr))
                    return null;

                var credential = Marshal.PtrToStructure<CREDENTIAL>(credPtr);

                if (credential.CredentialBlobSize == 0 || credential.CredentialBlob == IntPtr.Zero)
                    return null;

                // Convert credential blob to SecureString
                SecureString securePassword = new SecureString();
                int charCount = (int)credential.CredentialBlobSize / 2; // Unicode chars

                for (int i = 0; i < charCount; i++)
                {
                    char c = (char)Marshal.ReadInt16(credential.CredentialBlob, i * 2);
                    securePassword.AppendChar(c);
                }

                securePassword.MakeReadOnly();
                return securePassword;
            }
            catch
            {
                return null;
            }
            finally
            {
                if (credPtr != IntPtr.Zero)
                    CredFree(credPtr);
            }
        }

        /// <summary>
        /// Deletes a stored password from the Windows Credential Manager.
        /// </summary>
        /// <param name="rootDirectory">The encrypted directory path (used as identifier)</param>
        /// <returns>True if successful or not found, false on error</returns>
        public static bool DeletePassword(string rootDirectory)
        {
            if (string.IsNullOrEmpty(rootDirectory))
                return false;

            string targetName = GetTargetName(rootDirectory);
            
            // CredDeleteW returns false if credential doesn't exist, which is fine
            CredDeleteW(targetName, CRED_TYPE_GENERIC, 0);
            return true;
        }

        /// <summary>
        /// Checks if a password is stored for the given directory.
        /// </summary>
        /// <param name="rootDirectory">The encrypted directory path</param>
        /// <returns>True if a password is stored</returns>
        public static bool HasStoredPassword(string rootDirectory)
        {
            if (string.IsNullOrEmpty(rootDirectory))
                return false;

            string targetName = GetTargetName(rootDirectory);
            IntPtr credPtr = IntPtr.Zero;

            try
            {
                bool result = CredReadW(targetName, CRED_TYPE_GENERIC, 0, out credPtr);
                return result;
            }
            finally
            {
                if (credPtr != IntPtr.Zero)
                    CredFree(credPtr);
            }
        }
    }
}
