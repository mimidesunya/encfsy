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
        private string settingsFile = Application.LocalUserAppDataPath + "\\settings.txt";
        private string rootOptionsFile = Application.LocalUserAppDataPath + "\\root_options.txt";
        private string encfsExecutable = Path.GetDirectoryName(Application.ExecutablePath) + "\\encfs.exe";
        private bool isAdvancedMode = false;
        private bool isInitializing = true;
        private bool isLoadingOptions = false;
        private Dictionary<string, RootPathOptions> rootPathOptionsCache = new Dictionary<string, RootPathOptions>(StringComparer.OrdinalIgnoreCase);

        /// <summary>
        /// Stores options for a specific root path.
        /// </summary>
        private class RootPathOptions
        {
            // Basic options
            public bool AltStream { get; set; } = true;
            public bool MountManager { get; set; } = true;
            public bool CaseInsensitive { get; set; } = true;
            public bool ReadOnly { get; set; } = false;
            public bool Reverse { get; set; } = false;

            // Advanced options
            public bool Paranoia { get; set; } = false;
            public bool Removable { get; set; } = false;
            public bool CurrentSession { get; set; } = false;
            public bool FileLockUserMode { get; set; } = false;
            public bool EnableUnmountNetwork { get; set; } = false;
            public bool AllowIpcBatching { get; set; } = false;
            public bool CloudConflict { get; set; } = false;

            // Advanced settings
            public int Timeout { get; set; } = 120000;
            public string VolumeName { get; set; } = "";
            public string VolumeSerial { get; set; } = "";
            public string NetworkUnc { get; set; } = "";
            public int AllocationUnitSize { get; set; } = 0;
            public int SectorSize { get; set; } = 0;

            public string Serialize()
            {
                var parts = new List<string>
                {
                    AltStream ? "1" : "0",
                    MountManager ? "1" : "0",
                    CaseInsensitive ? "1" : "0",
                    ReadOnly ? "1" : "0",
                    Reverse ? "1" : "0",
                    Paranoia ? "1" : "0",
                    Removable ? "1" : "0",
                    CurrentSession ? "1" : "0",
                    FileLockUserMode ? "1" : "0",
                    EnableUnmountNetwork ? "1" : "0",
                    AllowIpcBatching ? "1" : "0",
                    CloudConflict ? "1" : "0",
                    Timeout.ToString(),
                    VolumeName ?? string.Empty,
                    VolumeSerial ?? string.Empty,
                    NetworkUnc ?? string.Empty,
                    AllocationUnitSize.ToString(),
                    SectorSize.ToString()
                };
                return string.Join("|", parts);
            }

            public static RootPathOptions Deserialize(string data)
            {
                var options = new RootPathOptions();
                if (string.IsNullOrEmpty(data))
                    return options;

                var parts = data.Split('|');
                
                // Current format (12 boolean flags):
                // 0: AltStream, 1: MountManager, 2: CaseInsensitive, 3: ReadOnly, 4: Reverse,
                // 5: Paranoia, 6: Removable, 7: CurrentSession, 8: FileLockUserMode,
                // 9: EnableUnmountNetwork, 10: AllowIpcBatching, 11: CloudConflict,
                // 12: Timeout, 13: VolumeName, 14: VolumeSerial, 15: NetworkUnc,
                // 16: AllocationUnitSize, 17: SectorSize
                //
                // Legacy format (14 boolean flags - had DebugMode and UseStdErr):
                // Same as above but with 2 extra debug flags at indices 12 and 13

                options.AltStream = GetBool(parts, 0);
                options.MountManager = GetBool(parts, 1);
                options.CaseInsensitive = GetBool(parts, 2);
                options.ReadOnly = GetBool(parts, 3);
                options.Reverse = GetBool(parts, 4);
                options.Paranoia = GetBool(parts, 5);
                options.Removable = GetBool(parts, 6);
                options.CurrentSession = GetBool(parts, 7);
                options.FileLockUserMode = GetBool(parts, 8);
                options.EnableUnmountNetwork = GetBool(parts, 9);
                options.AllowIpcBatching = GetBool(parts, 10);
                options.CloudConflict = GetBool(parts, 11);

                // Detect legacy format: if indices 12 and 13 are both binary flags (0 or 1),
                // this is likely the old format with debug flags
                bool hasLegacyDebugFlags = parts.Length >= 20 && IsBinaryFlag(parts, 12) && IsBinaryFlag(parts, 13);
                
                int timeoutIndex = hasLegacyDebugFlags ? 14 : 12;
                int volumeNameIndex = timeoutIndex + 1;
                int volumeSerialIndex = timeoutIndex + 2;
                int networkIndex = timeoutIndex + 3;
                int allocationIndex = timeoutIndex + 4;
                int sectorIndex = timeoutIndex + 5;

                if (TryParseInt(parts, timeoutIndex, out int timeout))
                    options.Timeout = timeout;
                if (volumeNameIndex < parts.Length)
                    options.VolumeName = parts[volumeNameIndex] ?? "";
                if (volumeSerialIndex < parts.Length)
                    options.VolumeSerial = parts[volumeSerialIndex] ?? "";
                if (networkIndex < parts.Length)
                {
                    string networkValue = parts[networkIndex] ?? "";
                    // NetworkUnc should be a UNC path (e.g., \\server\share), not a number
                    // If it's just a number (like "0"), treat it as empty
                    if (!IsNumericOnly(networkValue))
                    {
                        options.NetworkUnc = networkValue;
                    }
                }
                if (TryParseInt(parts, allocationIndex, out int allocUnit))
                    options.AllocationUnitSize = allocUnit;
                if (TryParseInt(parts, sectorIndex, out int sectorSize))
                    options.SectorSize = sectorSize;

                return options;
            }

            private static bool GetBool(string[] parts, int index)
            {
                return index < parts.Length && parts[index] == "1";
            }

            private static bool IsBinaryFlag(string[] parts, int index)
            {
                return index < parts.Length && (parts[index] == "0" || parts[index] == "1");
            }

            private static bool IsNumericOnly(string value)
            {
                if (string.IsNullOrEmpty(value))
                    return false;
                foreach (char c in value)
                {
                    if (!char.IsDigit(c))
                        return false;
                }
                return true;
            }

            private static bool TryParseInt(string[] parts, int index, out int value)
            {
                value = 0;
                return index < parts.Length && int.TryParse(parts[index], out value);
            }
        }

        public MainForm()
        {
            InitializeComponent();
            LoadSettings();
            LoadRootPathOptions();
            InitializeLanguageComboBox();
            ApplyLocalization();
            UpdateAdvancedModeVisibility();
            isInitializing = false;
        }

        /// <summary>
        /// Initialize the language combo box with available languages.
        /// </summary>
        private void InitializeLanguageComboBox()
        {
            languageComboBox.Items.Clear();
            foreach (var lang in Strings.AvailableLanguages)
            {
                languageComboBox.Items.Add(new LanguageItem(lang.Key, lang.Value));
            }

            // Select current language
            for (int i = 0; i < languageComboBox.Items.Count; i++)
            {
                if (((LanguageItem)languageComboBox.Items[i]).Code == Strings.CurrentLanguage)
                {
                    languageComboBox.SelectedIndex = i;
                    break;
                }
            }
        }

        /// <summary>
        /// Helper class to store language code and display name.
        /// </summary>
        private class LanguageItem
        {
            public string Code { get; }
            public string DisplayName { get; }

            public LanguageItem(string code, string displayName)
            {
                Code = code;
                DisplayName = displayName;
            }

            public override string ToString()
            {
                return DisplayName;
            }
        }

        /// <summary>
        /// Apply localized strings to all UI elements.
        /// </summary>
        private void ApplyLocalization()
        {
            // Window title
            this.Text = Strings.AppTitle;

            // Group boxes
            driveGroup.Text = Strings.DriveSelection;
            directoryGroup.Text = Strings.EncryptedDirectory;
            basicOptionsGroup.Text = Strings.Options;
            advancedOptionsGroup.Text = Strings.AdvancedOptions;
            advancedSettingsGroup.Text = Strings.Settings;
            commandPreviewGroup.Text = Strings.CommandPreview;

            // Buttons
            mountButton.Text = Strings.Mount;
            unmountButton.Text = Strings.Unmount;
            selectDirectoryButton.Text = Strings.Browse;
            refreshButton.Text = Strings.Refresh;
            copyCommandButton.Text = Strings.Copy;

            // Column headers
            driveColumn.Text = Strings.Drive;
            statusColumn.Text = Strings.Status;

            // Checkboxes
            altStreamCheckBox.Text = Strings.AltStream;
            mountManagerCheckBox.Text = Strings.MountManager;
            caseInsensitiveCheckBox.Text = Strings.IgnoreCase;
            readOnlyCheckBox.Text = Strings.ReadOnly;
            reverseCheckBox.Text = Strings.Reverse;
            paranoiaCheckBox.Text = Strings.Paranoia;
            removableCheckBox.Text = Strings.Removable;
            currentSessionCheckBox.Text = Strings.CurrentSession;
            fileLockUserModeCheckBox.Text = Strings.FileLockUserMode;
            enableUnmountNetworkCheckBox.Text = Strings.EnableUnmountNetwork;
            allowIpcBatchingCheckBox.Text = Strings.AllowIpcBatching;
            cloudConflictCheckBox.Text = Strings.CloudConflict;

            // Labels
            timeoutLabel.Text = Strings.Timeout;
            volumeNameLabel.Text = Strings.VolumeName;
            volumeSerialLabel.Text = Strings.VolumeSerial;
            networkUncLabel.Text = Strings.NetworkUnc;
            allocationUnitLabel.Text = Strings.AllocationUnit;
            sectorSizeLabel.Text = Strings.SectorSize;
            languageLabel.Text = Strings.Language;

            // Advanced mode checkbox
            advancedModeCheckBox.Text = isAdvancedMode 
                ? Strings.HideAdvancedOptions 
                : Strings.ShowAdvancedOptions;

            // Tooltips
            toolTip.SetToolTip(refreshButton, Strings.TooltipRefresh);
            toolTip.SetToolTip(altStreamCheckBox, Strings.TooltipAltStream);
            toolTip.SetToolTip(mountManagerCheckBox, Strings.TooltipMountManager);
            toolTip.SetToolTip(paranoiaCheckBox, Strings.TooltipParanoia);
            toolTip.SetToolTip(languageComboBox, Strings.TooltipLanguage);
            toolTip.SetToolTip(cloudConflictCheckBox, Strings.TooltipCloudConflict);
        }

        /// <summary>
        /// Load settings from file.
        /// </summary>
        private void LoadSettings()
        {
            try
            {
                if (File.Exists(settingsFile))
                {
                    using (StreamReader reader = new StreamReader(settingsFile, Encoding.UTF8))
                    {
                        string line;
                        while ((line = reader.ReadLine()) != null)
                        {
                            if (line.StartsWith("language="))
                            {
                                string lang = line.Substring("language=".Length);
                                Strings.SetLanguage(lang);
                            }
                        }
                    }
                }
            }
            catch (Exception)
            {
                // Ignore settings load errors, use system default
            }
        }

        /// <summary>
        /// Save settings to file.
        /// </summary>
        private void SaveSettings()
        {
            try
            {
                using (StreamWriter writer = new StreamWriter(settingsFile, false, Encoding.UTF8))
                {
                    writer.WriteLine("language=" + Strings.CurrentLanguage);
                }
            }
            catch (Exception)
            {
                // Ignore settings save errors
            }
        }

        /// <summary>
        /// Load root path options from file.
        /// </summary>
        private void LoadRootPathOptions()
        {
            rootPathOptionsCache.Clear();
            try
            {
                if (File.Exists(rootOptionsFile))
                {
                    using (StreamReader reader = new StreamReader(rootOptionsFile, Encoding.UTF8))
                    {
                        string line;
                        while ((line = reader.ReadLine()) != null)
                        {
                            int separatorIndex = line.IndexOf('\t');
                            if (separatorIndex > 0)
                            {
                                string rootPath = line.Substring(0, separatorIndex);
                                string optionsData = line.Substring(separatorIndex + 1);
                                rootPathOptionsCache[rootPath] = RootPathOptions.Deserialize(optionsData);
                            }
                        }
                    }
                }
            }
            catch (Exception)
            {
                // Ignore load errors
            }
        }

        /// <summary>
        /// Save all root path options to file.
        /// </summary>
        private void SaveRootPathOptions()
        {
            try
            {
                using (StreamWriter writer = new StreamWriter(rootOptionsFile, false, Encoding.UTF8))
                {
                    foreach (var kvp in rootPathOptionsCache)
                    {
                        writer.WriteLine($"{kvp.Key}\t{kvp.Value.Serialize()}");
                    }
                }
            }
            catch (Exception)
            {
                // Ignore save errors
            }
        }

        /// <summary>
        /// Get options for the current root path from UI.
        /// </summary>
        private RootPathOptions GetCurrentOptions()
        {
            return new RootPathOptions
            {
                // Basic options
                AltStream = altStreamCheckBox.Checked,
                MountManager = mountManagerCheckBox.Checked,
                CaseInsensitive = caseInsensitiveCheckBox.Checked,
                ReadOnly = readOnlyCheckBox.Checked,
                Reverse = reverseCheckBox.Checked,

                // Advanced options
                Paranoia = paranoiaCheckBox.Checked,
                Removable = removableCheckBox.Checked,
                CurrentSession = currentSessionCheckBox.Checked,
                FileLockUserMode = fileLockUserModeCheckBox.Checked,
                EnableUnmountNetwork = enableUnmountNetworkCheckBox.Checked,
                AllowIpcBatching = allowIpcBatchingCheckBox.Checked,
                CloudConflict = cloudConflictCheckBox.Checked,

                // Advanced settings
                Timeout = (int)timeoutNumeric.Value,
                VolumeName = volumeNameTextBox.Text,
                VolumeSerial = volumeSerialTextBox.Text,
                NetworkUnc = networkUncTextBox.Text,
                AllocationUnitSize = (int)allocationUnitNumeric.Value,
                SectorSize = (int)sectorSizeNumeric.Value
            };
        }

        /// <summary>
        /// Apply options to UI controls.
        /// </summary>
        private void ApplyOptions(RootPathOptions options)
        {
            isLoadingOptions = true;
            try
            {
                // Basic options
                altStreamCheckBox.Checked = options.AltStream;
                mountManagerCheckBox.Checked = options.MountManager;
                caseInsensitiveCheckBox.Checked = options.CaseInsensitive;
                readOnlyCheckBox.Checked = options.ReadOnly;
                reverseCheckBox.Checked = options.Reverse;

                // Advanced options
                paranoiaCheckBox.Checked = options.Paranoia;
                removableCheckBox.Checked = options.Removable;
                currentSessionCheckBox.Checked = options.CurrentSession;
                fileLockUserModeCheckBox.Checked = options.FileLockUserMode;
                enableUnmountNetworkCheckBox.Checked = options.EnableUnmountNetwork;
                allowIpcBatchingCheckBox.Checked = options.AllowIpcBatching;
                cloudConflictCheckBox.Checked = options.CloudConflict;

                // Advanced settings
                timeoutNumeric.Value = Math.Max(timeoutNumeric.Minimum, Math.Min(timeoutNumeric.Maximum, options.Timeout));
                volumeNameTextBox.Text = options.VolumeName ?? "";
                volumeSerialTextBox.Text = options.VolumeSerial ?? "";
                networkUncTextBox.Text = options.NetworkUnc ?? "";
                allocationUnitNumeric.Value = Math.Max(allocationUnitNumeric.Minimum, Math.Min(allocationUnitNumeric.Maximum, options.AllocationUnitSize));
                sectorSizeNumeric.Value = Math.Max(sectorSizeNumeric.Minimum, Math.Min(sectorSizeNumeric.Maximum, options.SectorSize));
            }
            finally
            {
                isLoadingOptions = false;
            }
        }

        /// <summary>
        /// Save options for the current root path.
        /// </summary>
        private void SaveCurrentRootPathOptions()
        {
            string rootPath = rootPathCombo.Text;
            if (!string.IsNullOrWhiteSpace(rootPath))
            {
                rootPathOptionsCache[rootPath] = GetCurrentOptions();
                SaveRootPathOptions();
            }
        }

        /// <summary>
        /// Load and apply options for the specified root path.
        /// </summary>
        private void LoadAndApplyRootPathOptions(string rootPath)
        {
            if (string.IsNullOrWhiteSpace(rootPath)) return;

            if (rootPathOptionsCache.TryGetValue(rootPath, out RootPathOptions options))
            {
                ApplyOptions(options);
            }
            else
            {
                // Apply default options for new root path
                ApplyOptions(new RootPathOptions());
            }
        }

        #region Event Handlers

        private void MainForm_Load(object sender, EventArgs e)
        {
            UpdateDrives();
            UpdateCommandPreview();
        }

        private void languageComboBox_SelectedIndexChanged(object sender, EventArgs e)
        {
            if (isInitializing) return;

            var selectedItem = languageComboBox.SelectedItem as LanguageItem;
            if (selectedItem != null && selectedItem.Code != Strings.CurrentLanguage)
            {
                Strings.SetLanguage(selectedItem.Code);
                SaveSettings();
                ApplyLocalization();
            }
        }

        private void selectDirectoryButton_Click(object sender, EventArgs e)
        {
            // Save current options before changing root path
            if (!isInitializing && !isLoadingOptions && !string.IsNullOrWhiteSpace(rootPathCombo.Text))
            {
                SaveCurrentRootPathOptions();
            }

            using (FolderBrowserDialog dialog = new FolderBrowserDialog())
            {
                dialog.Description = Strings.SelectEncryptedDirectory;
                if (dialog.ShowDialog() == DialogResult.OK)
                {
                    rootPathCombo.Text = dialog.SelectedPath;
                    LoadAndApplyRootPathOptions(dialog.SelectedPath);
                    UpdateButtons();
                    UpdateCommandPreview();
                }
            }
        }

        private void mountButton_Click(object sender, EventArgs e)
        {
            if (driveListView.SelectedItems.Count == 0)
            {
                MessageBox.Show(Strings.SelectDriveError, Strings.Error, MessageBoxButtons.OK, MessageBoxIcon.Warning);
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
                    MessageBox.Show(Strings.PasswordEmptyError, Strings.Error, MessageBoxButtons.OK, MessageBoxIcon.Warning);
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

        private string previousRootPath = "";

        private void rootPathCombo_TextChanged(object sender, EventArgs e)
        {
            // Save options for previous root path before switching
            if (!isInitializing && !isLoadingOptions && !string.IsNullOrWhiteSpace(previousRootPath) && previousRootPath != rootPathCombo.Text)
            {
                rootPathOptionsCache[previousRootPath] = GetCurrentOptions();
                SaveRootPathOptions();
            }

            // Load options for new root path
            string currentPath = rootPathCombo.Text;
            if (!isInitializing && !isLoadingOptions && !string.IsNullOrWhiteSpace(currentPath) && currentPath != previousRootPath)
            {
                LoadAndApplyRootPathOptions(currentPath);
            }

            previousRootPath = currentPath;
            UpdateButtons();
            UpdateCommandPreview();
        }

        private void anyOption_Changed(object sender, EventArgs e)
        {
            // Save options when any option changes (not during initialization or loading)
            if (!isInitializing && !isLoadingOptions)
            {
                SaveCurrentRootPathOptions();
            }
            UpdateCommandPreview();
        }

        private void copyCommandButton_Click(object sender, EventArgs e)
        {
            if (!string.IsNullOrEmpty(commandPreviewTextBox.Text))
            {
                Clipboard.SetText(commandPreviewTextBox.Text);
                toolTip.Show(Strings.Copied, copyCommandButton, 0, -20, 1500);
            }
        }

        #endregion

        #region Helper Methods

        private void UpdateAdvancedModeVisibility()
        {
            // Update checkbox text to indicate expand/collapse state
            advancedModeCheckBox.Text = isAdvancedMode 
                ? Strings.HideAdvancedOptions 
                : Strings.ShowAdvancedOptions;
            
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
                if (reverseCheckBox.Checked)
                    args.Append(" --reverse");
                if (cloudConflictCheckBox.Checked)
                    args.Append(" --cloud-conflict");
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
                            MessageBox.Show(this, string.Format(Strings.MountResult, encfsOut), "EncFS", 
                                MessageBoxButtons.OK, MessageBoxIcon.Information);
                        });
                    }
                }
                catch (Exception ex)
                {
                    this.Invoke((MethodInvoker)delegate
                    {
                        MessageBox.Show(this, $"{Strings.Error}: {ex.Message}", Strings.Error, 
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
                                MessageBox.Show(this, string.Format(Strings.MountFailed, message), Strings.Error,
                                    MessageBoxButtons.OK, MessageBoxIcon.Error);
                            });
                        }
                    }
                    // Success case - only show message if it's NOT "Success"
                    else if (!string.IsNullOrEmpty(output) && !output.Equals("Success", StringComparison.OrdinalIgnoreCase))
                    {
                        this.Invoke((MethodInvoker)delegate
                        {
                            MessageBox.Show(this, string.Format(Strings.MountResult, output), "EncFS",
                                MessageBoxButtons.OK, MessageBoxIcon.Information);
                        });
                    }
                    // If output is "Success" or empty with exit code 0, do nothing (success)
                }
                catch (Exception ex)
                {
                    this.Invoke((MethodInvoker)delegate
                    {
                        MessageBox.Show(this, $"{Strings.Error}: {ex.Message}", Strings.Error,
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