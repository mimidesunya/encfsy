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
using System.Threading;
using System.Diagnostics;
using System.IO;

namespace EncFSy_gui
{
    public partial class MainForm : Form
    {
        private string historyFile = Application.LocalUserAppDataPath + "\\history.txt";
        private string encfsExecutable = Path.GetDirectoryName(Application.ExecutablePath) + "\\encfs.exe";
        private bool isAdvancedMode = false;

        public MainForm()
        {
            InitializeComponent();
            UpdateAdvancedModeVisibility();
        }

        #region Event Handlers

        private void MainForm_Load(object sender, EventArgs e)
        {
            UpdateDrives();
            UpdateCommandPreview();
        }

        private void selectDirectoryButton_Click(object sender, EventArgs e)
        {
            using (FolderBrowserDialog dialog = new FolderBrowserDialog())
            {
                dialog.Description = "Select the encrypted directory (rootDir)";
                if (dialog.ShowDialog() == DialogResult.OK)
                {
                    rootPathCombo.Text = dialog.SelectedPath;
                    UpdateButtons();
                    UpdateCommandPreview();
                }
            }
        }

        private void mountButton_Click(object sender, EventArgs e)
        {
            if (driveListView.SelectedItems.Count == 0)
            {
                MessageBox.Show("Please select a drive letter.", "Error", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            string rootPath = rootPathCombo.Text;
            SaveHistory();

            string drive = driveListView.SelectedItems[0].Text.Substring(0, 1);

            using (PasswordForm passwordForm = new PasswordForm())
            {
                passwordForm.StartPosition = FormStartPosition.CenterParent;
                passwordForm.SetRootDirectory(rootPath);
                
                if (passwordForm.ShowDialog(this) != DialogResult.OK)
                {
                    return;
                }

                SecureString securePassword = passwordForm.SecurePassword;
                if (securePassword == null || securePassword.Length == 0)
                {
                    MessageBox.Show("Password cannot be empty.", "Error", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }

                bool rememberPassword = passwordForm.RememberPassword;
                
                // Always save password to Credential Manager for secure transfer to encfs.exe
                // encfs.exe will delete it if --use-credential-once is specified
                CredentialManager.SavePassword(rootPath, securePassword);

                string args = BuildCommandArguments(rootPath, drive);
                
                if (rememberPassword)
                {
                    // Keep password stored - use --use-credential
                    args += " --use-credential";
                }
                else
                {
                    // Delete password after reading - use --use-credential-once
                    args += " --use-credential-once";
                }
                
                ExecuteMountWithCredential(args);
            }

            Thread.Sleep(3000);
            UpdateDrives();
        }

        private void unmountButton_Click(object sender, EventArgs e)
        {
            if (driveListView.SelectedItems.Count == 0) return;

            string drive = driveListView.SelectedItems[0].Text.Substring(0, 1);

            using (Process process = StartEncFS("-u " + drive))
            {
                process.WaitForExit();
            }
            UpdateDrives();
        }

        private void refreshButton_Click(object sender, EventArgs e)
        {
            UpdateDrives();
        }

        private void advancedModeCheckBox_CheckedChanged(object sender, EventArgs e)
        {
            isAdvancedMode = advancedModeCheckBox.Checked;
            UpdateAdvancedModeVisibility();
            UpdateCommandPreview();
        }

        private void driveListView_SelectedIndexChanged(object sender, EventArgs e)
        {
            UpdateButtons();
            UpdateCommandPreview();
        }

        private void rootPathCombo_TextChanged(object sender, EventArgs e)
        {
            UpdateButtons();
            UpdateCommandPreview();
        }

        private void anyOption_Changed(object sender, EventArgs e)
        {
            UpdateCommandPreview();
        }

        private void copyCommandButton_Click(object sender, EventArgs e)
        {
            if (!string.IsNullOrEmpty(commandPreviewTextBox.Text))
            {
                Clipboard.SetText(commandPreviewTextBox.Text);
                toolTip.Show("Copied!", copyCommandButton, 0, -20, 1500);
            }
        }

        #endregion

        #region Helper Methods

        private void UpdateAdvancedModeVisibility()
        {
            // Update checkbox text to indicate expand/collapse state
            advancedModeCheckBox.Text = isAdvancedMode 
                ? "Hide Advanced Options £" 
                : "Show Advanced Options ¥";
            
            // Show/hide advanced controls - form will auto-resize
            advancedOptionsGroup.Visible = isAdvancedMode;
            advancedSettingsGroup.Visible = isAdvancedMode;
            commandPreviewGroup.Visible = isAdvancedMode;
        }

        private Process StartEncFS(string args)
        {
            Process process = new Process();
            ProcessStartInfo startInfo = new ProcessStartInfo
            {
                FileName = encfsExecutable,
                Arguments = args,
                CreateNoWindow = true,
                RedirectStandardInput = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false
            };
            process.StartInfo = startInfo;
            process.Start();
            return process;
        }

        private string BuildCommandArguments(string rootPath, string drive)
        {
            StringBuilder args = new StringBuilder();
            args.Append($"\"{rootPath}\" {drive}");

            // Basic options
            if (altStreamCheckBox.Checked)
                args.Append(" --alt-stream");
            if (mountManagerCheckBox.Checked)
                args.Append(" --dokan-mount-manager");
            if (caseInsensitiveCheckBox.Checked)
                args.Append(" --case-insensitive");
            if (readOnlyCheckBox.Checked)
                args.Append(" --dokan-write-protect");
            if (reverseCheckBox.Checked)
                args.Append(" --reverse");

            // Advanced options (only if advanced mode is enabled)
            if (isAdvancedMode)
            {
                if (paranoiaCheckBox.Checked)
                    args.Append(" --paranoia");
                if (removableCheckBox.Checked)
                    args.Append(" --dokan-removable");
                if (currentSessionCheckBox.Checked)
                    args.Append(" --dokan-current-session");
                if (fileLockUserModeCheckBox.Checked)
                    args.Append(" --dokan-filelock-user-mode");
                if (enableUnmountNetworkCheckBox.Checked)
                    args.Append(" --dokan-enable-unmount-network-drive");
                if (allowIpcBatchingCheckBox.Checked)
                    args.Append(" --dokan-allow-ipc-batching");
                if (debugModeCheckBox.Checked)
                    args.Append(" -v");
                if (stderrCheckBox.Checked)
                    args.Append(" -s");

                // Timeout
                if (timeoutNumeric.Value != 120000)
                    args.Append($" -i {(int)timeoutNumeric.Value}");

                // Volume name
                if (!string.IsNullOrWhiteSpace(volumeNameTextBox.Text))
                    args.Append($" --volume-name \"{volumeNameTextBox.Text}\"");

                // Volume serial
                if (!string.IsNullOrWhiteSpace(volumeSerialTextBox.Text))
                    args.Append($" --volume-serial {volumeSerialTextBox.Text}");

                // Network UNC
                if (!string.IsNullOrWhiteSpace(networkUncTextBox.Text))
                    args.Append($" --dokan-network \"{networkUncTextBox.Text}\"");

                // Allocation unit size
                if (allocationUnitNumeric.Value > 0)
                    args.Append($" --allocation-unit-size {(int)allocationUnitNumeric.Value}");

                // Sector size
                if (sectorSizeNumeric.Value > 0)
                    args.Append($" --sector-size {(int)sectorSizeNumeric.Value}");
            }

            return args.ToString();
        }

        private void UpdateCommandPreview()
        {
            if (!isAdvancedMode)
            {
                commandPreviewTextBox.Text = string.Empty;
                return;
            }

            string drive = driveListView.SelectedItems.Count > 0
                ? driveListView.SelectedItems[0].Text.Substring(0, 1)
                : "M";

            string rootPath = string.IsNullOrEmpty(rootPathCombo.Text)
                ? "C:\\path\\to\\encrypted"
                : rootPathCombo.Text;

            string args = BuildCommandArguments(rootPath, drive);
            commandPreviewTextBox.Text = $"encfs.exe {args}";
        }

        private void ExecuteMount(string args, SecureString securePassword)
        {
            Process process = StartEncFS(args);

            Thread inputThread = new Thread(() =>
            {
                // Convert SecureString to string for process input
                IntPtr unmanagedString = IntPtr.Zero;
                try
                {
                    unmanagedString = Marshal.SecureStringToGlobalAllocUnicode(securePassword);
                    string password = Marshal.PtrToStringUni(unmanagedString);
                    process.StandardInput.WriteLine(password);
                    
                    // Clear the password string from memory
                    password = null;
                }
                finally
                {
                    if (unmanagedString != IntPtr.Zero)
                        Marshal.ZeroFreeGlobalAllocUnicode(unmanagedString);
                }
            });
            inputThread.Start();

            Thread outputThread = new Thread(() =>
            {
                try
                {
                    string encfsOut = process.StandardOutput.ReadLine();
                    encfsOut = process.StandardOutput.ReadLine();

                    if (encfsOut != null && encfsOut.Equals("Enter new password: "))
                    {
                        // Convert SecureString to string for process input (new password confirmation)
                        IntPtr unmanagedString = IntPtr.Zero;
                        try
                        {
                            unmanagedString = Marshal.SecureStringToGlobalAllocUnicode(securePassword);
                            string password = Marshal.PtrToStringUni(unmanagedString);
                            process.StandardInput.WriteLine(password);
                            password = null;
                        }
                        finally
                        {
                            if (unmanagedString != IntPtr.Zero)
                                Marshal.ZeroFreeGlobalAllocUnicode(unmanagedString);
                        }
                        
                        encfsOut = process.StandardOutput.ReadLine();
                        encfsOut = process.StandardOutput.ReadLine();
                    }

                    if (encfsOut != null && !encfsOut.Equals("Success"))
                    {
                        this.Invoke((MethodInvoker)delegate
                        {
                            MessageBox.Show(this, $"Mount result: '{encfsOut}'", "EncFS", 
                                MessageBoxButtons.OK, MessageBoxIcon.Information);
                        });
                    }
                }
                catch (Exception ex)
                {
                    this.Invoke((MethodInvoker)delegate
                    {
                        MessageBox.Show(this, $"Error: {ex.Message}", "EncFS Error", 
                            MessageBoxButtons.OK, MessageBoxIcon.Error);
                    });
                }
            });
            outputThread.IsBackground = true;
            outputThread.Start();
        }

        /// <summary>
        /// Executes mount using Windows Credential Manager (no password over stdin).
        /// This is more secure as the password never leaves the Credential Manager.
        /// </summary>
        private void ExecuteMountWithCredential(string args)
        {
            Process process = StartEncFS(args);

            Thread outputThread = new Thread(() =>
            {
                try
                {
                    // Read all output
                    string output = process.StandardOutput.ReadToEnd();
                    string error = process.StandardError.ReadToEnd();
                    process.WaitForExit();

                    // Trim whitespace from output
                    output = output?.Trim() ?? "";
                    error = error?.Trim() ?? "";

                    // Check for errors (non-zero exit code or stderr output)
                    if (process.ExitCode != 0)
                    {
                        string message = !string.IsNullOrEmpty(error) ? error : output;
                        if (!string.IsNullOrEmpty(message))
                        {
                            this.Invoke((MethodInvoker)delegate
                            {
                                MessageBox.Show(this, $"Mount failed: {message}", "EncFS Error",
                                    MessageBoxButtons.OK, MessageBoxIcon.Error);
                            });
                        }
                    }
                    // Success case - only show message if it's NOT "Success"
                    else if (!string.IsNullOrEmpty(output) && !output.Equals("Success", StringComparison.OrdinalIgnoreCase))
                    {
                        this.Invoke((MethodInvoker)delegate
                        {
                            MessageBox.Show(this, $"Mount result: '{output}'", "EncFS",
                                MessageBoxButtons.OK, MessageBoxIcon.Information);
                        });
                    }
                    // If output is "Success" or empty with exit code 0, do nothing (success)
                }
                catch (Exception ex)
                {
                    this.Invoke((MethodInvoker)delegate
                    {
                        MessageBox.Show(this, $"Error: {ex.Message}", "EncFS Error",
                            MessageBoxButtons.OK, MessageBoxIcon.Error);
                    });
                }
            });
            outputThread.IsBackground = true;
            outputThread.Start();
        }

        private void SaveHistory()
        {
            try
            {
                using (StreamWriter writer = new StreamWriter(historyFile, false, Encoding.UTF8))
                {
                    writer.WriteLine(rootPathCombo.Text);
                    foreach (string item in rootPathCombo.Items)
                    {
                        if (!item.Equals(rootPathCombo.Text))
                        {
                            writer.WriteLine(item);
                        }
                    }
                }
            }
            catch (Exception)
            {
                // Ignore history save errors
            }
        }

        private void UpdateDrives()
        {
            // Load history
            rootPathCombo.Items.Clear();
            if (File.Exists(historyFile))
            {
                try
                {
                    using (StreamReader reader = new StreamReader(historyFile, Encoding.UTF8))
                    {
                        string line;
                        while ((line = reader.ReadLine()) != null)
                        {
                            rootPathCombo.Items.Add(line);
                        }
                        if (rootPathCombo.Items.Count > 0)
                        {
                            rootPathCombo.SelectedIndex = 0;
                        }
                    }
                }
                catch (Exception)
                {
                    // Ignore history load errors
                }
            }

            // Get mounted EncFS drives
            List<string> encfsDrives = new List<string>();
            try
            {
                using (Process process = StartEncFS("-l"))
                {
                    string line;
                    while ((line = process.StandardOutput.ReadLine()) != null)
                    {
                        if (line.Length >= 2)
                        {
                            encfsDrives.Add(line.Substring(line.Length - 2));
                        }
                    }
                }
            }
            catch (Exception)
            {
                // Ignore errors when listing mounts
            }

            // Update drive list
            driveListView.Items.Clear();
            driveListView.View = View.Details;

            for (char i = 'A'; i <= 'Z'; ++i)
            {
                string drive = i + ":";
                if (Directory.Exists(drive + "\\"))
                {
                    if (encfsDrives.Contains(drive))
                    {
                        // Get volume label for mounted EncFS drive
                        string volumeLabel = "";
                        try
                        {
                            DriveInfo driveInfo = new DriveInfo(i.ToString());
                            if (driveInfo.IsReady && !string.IsNullOrEmpty(driveInfo.VolumeLabel))
                            {
                                volumeLabel = driveInfo.VolumeLabel;
                            }
                        }
                        catch (Exception)
                        {
                            // Ignore errors when getting volume label
                        }

                        ListViewItem item = new ListViewItem(new[] { drive, volumeLabel });
                        item.BackColor = Color.LightGreen;
                        driveListView.Items.Add(item);
                    }
                    // Skip other existing drives
                }
                else
                {
                    ListViewItem item = new ListViewItem(new[] { drive, "" });
                    driveListView.Items.Add(item);
                }
            }

            UpdateButtons();
        }

        private void UpdateButtons()
        {
            bool hasSelection = driveListView.SelectedItems.Count > 0;
            bool isEncFSMount = hasSelection && driveListView.SelectedItems[0].BackColor == Color.LightGreen;
            bool hasValidPath = Directory.Exists(rootPathCombo.Text);

            mountButton.Enabled = hasSelection && !isEncFSMount && hasValidPath;
            unmountButton.Enabled = hasSelection && isEncFSMount;
        }

        #endregion
    }
}