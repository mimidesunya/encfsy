using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace EncFSy_gui
{
    public partial class PasswordForm : Form
    {
        private SecureString securePassword;
        private string rootDirectory;

        /// <summary>
        /// Gets the password as a regular string.
        /// Note: The caller should clear the string from memory as soon as possible.
        /// </summary>
        public string Password
        {
            get
            {
                if (securePassword == null || securePassword.Length == 0)
                    return null;
                
                return SecureStringToString(securePassword);
            }
        }

        /// <summary>
        /// Gets the password as a SecureString for more secure handling.
        /// </summary>
        public SecureString SecurePassword => securePassword;

        /// <summary>
        /// Gets whether the user wants to remember the password.
        /// </summary>
        public bool RememberPassword => rememberPassword.Checked;

        public PasswordForm()
        {
            InitializeComponent();
            ApplyLocalization();
            this.AcceptButton = this.okButton;
            this.CancelButton = this.cancelButton;
            securePassword = new SecureString();
        }

        /// <summary>
        /// Apply localized strings to all UI elements.
        /// </summary>
        private void ApplyLocalization()
        {
            this.Text = Strings.PasswordFormTitle;
            passwordLabel.Text = Strings.Password;
            showPassword.Text = Strings.ShowPassword;
            rememberPassword.Text = Strings.RememberPassword;
            okButton.Text = Strings.OK;
            cancelButton.Text = Strings.Cancel;
        }

        /// <summary>
        /// Sets the root directory for credential lookup.
        /// </summary>
        /// <param name="rootDir">The encrypted directory path</param>
        public void SetRootDirectory(string rootDir)
        {
            this.rootDirectory = rootDir;
        }

        private void okButton_Click(object sender, EventArgs e)
        {
            // Copy password to SecureString
            securePassword.Clear();
            foreach (char c in this.passwordText.Text)
            {
                securePassword.AppendChar(c);
            }
            securePassword.MakeReadOnly();
            
            // Note: Credential Manager save/delete is handled by MainForm
            // based on RememberPassword property
            
            // Clear the password from the TextBox
            this.passwordText.Text = new string('\0', this.passwordText.Text.Length);
            this.passwordText.Clear();
            
            this.DialogResult = DialogResult.OK;
            this.Close();
        }

        private void cancelButton_Click(object sender, EventArgs e)
        {
            // Clear and dispose SecureString
            if (securePassword != null)
            {
                securePassword.Dispose();
                securePassword = null;
            }
            
            // Clear the password from the TextBox
            if (!string.IsNullOrEmpty(this.passwordText.Text))
            {
                this.passwordText.Text = new string('\0', this.passwordText.Text.Length);
                this.passwordText.Clear();
            }
            
            this.DialogResult = DialogResult.Cancel;
            this.Close();
        }

        private void showPassword_CheckedChanged(object sender, EventArgs e)
        {
            this.passwordText.PasswordChar = this.showPassword.Checked ? '\0' : '●';
        }

        private void PasswordForm_Shown(object sender, EventArgs e)
        {
            // Try to load saved password from Credential Manager
            if (!string.IsNullOrEmpty(rootDirectory))
            {
                var savedPassword = CredentialManager.GetPassword(rootDirectory);
                if (savedPassword != null)
                {
                    // Convert SecureString to string for TextBox display
                    IntPtr ptr = IntPtr.Zero;
                    try
                    {
                        ptr = Marshal.SecureStringToGlobalAllocUnicode(savedPassword);
                        this.passwordText.Text = Marshal.PtrToStringUni(ptr);
                        this.rememberPassword.Checked = true;
                    }
                    finally
                    {
                        if (ptr != IntPtr.Zero)
                            Marshal.ZeroFreeGlobalAllocUnicode(ptr);
                        savedPassword.Dispose();
                    }
                }
            }
            
            this.passwordText.Focus();
            this.passwordText.SelectAll();
        }

        /// <summary>
        /// Converts a SecureString to a regular string.
        /// The caller is responsible for clearing the returned string from memory.
        /// </summary>
        private static string SecureStringToString(SecureString secureString)
        {
            if (secureString == null)
                return null;

            IntPtr unmanagedString = IntPtr.Zero;
            try
            {
                unmanagedString = Marshal.SecureStringToGlobalAllocUnicode(secureString);
                return Marshal.PtrToStringUni(unmanagedString);
            }
            finally
            {
                if (unmanagedString != IntPtr.Zero)
                    Marshal.ZeroFreeGlobalAllocUnicode(unmanagedString);
            }
        }
    }
}
