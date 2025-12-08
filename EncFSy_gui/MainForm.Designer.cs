namespace EncFSy_gui
{
    partial class MainForm
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.components = new System.ComponentModel.Container();
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(MainForm));
            this.toolTip = new System.Windows.Forms.ToolTip(this.components);
            
            // Main container
            this.mainContainer = new System.Windows.Forms.FlowLayoutPanel();
            
            // Drive selection group
            this.driveGroup = new System.Windows.Forms.GroupBox();
            this.driveListView = new System.Windows.Forms.ListView();
            this.driveColumn = new System.Windows.Forms.ColumnHeader();
            this.statusColumn = new System.Windows.Forms.ColumnHeader();
            this.refreshButton = new System.Windows.Forms.Button();
            
            // Directory selection group
            this.directoryGroup = new System.Windows.Forms.GroupBox();
            this.rootPathCombo = new System.Windows.Forms.ComboBox();
            this.selectDirectoryButton = new System.Windows.Forms.Button();
            
            // Basic options group
            this.basicOptionsGroup = new System.Windows.Forms.GroupBox();
            this.altStreamCheckBox = new System.Windows.Forms.CheckBox();
            this.mountManagerCheckBox = new System.Windows.Forms.CheckBox();
            this.caseInsensitiveCheckBox = new System.Windows.Forms.CheckBox();
            this.readOnlyCheckBox = new System.Windows.Forms.CheckBox();
            this.reverseCheckBox = new System.Windows.Forms.CheckBox();
            
            // Buttons panel
            this.buttonsPanel = new System.Windows.Forms.Panel();
            this.mountButton = new System.Windows.Forms.Button();
            this.unmountButton = new System.Windows.Forms.Button();
            
            // Advanced mode panel (separator)
            this.advancedModePanel = new System.Windows.Forms.Panel();
            this.advancedModeCheckBox = new System.Windows.Forms.CheckBox();
            this.separatorLabel = new System.Windows.Forms.Label();
            
            // Advanced options group
            this.advancedOptionsGroup = new System.Windows.Forms.GroupBox();
            this.paranoiaCheckBox = new System.Windows.Forms.CheckBox();
            this.removableCheckBox = new System.Windows.Forms.CheckBox();
            this.currentSessionCheckBox = new System.Windows.Forms.CheckBox();
            this.fileLockUserModeCheckBox = new System.Windows.Forms.CheckBox();
            this.enableUnmountNetworkCheckBox = new System.Windows.Forms.CheckBox();
            this.allowIpcBatchingCheckBox = new System.Windows.Forms.CheckBox();
            this.debugModeCheckBox = new System.Windows.Forms.CheckBox();
            this.stderrCheckBox = new System.Windows.Forms.CheckBox();
            
            // Advanced settings group
            this.advancedSettingsGroup = new System.Windows.Forms.GroupBox();
            this.timeoutLabel = new System.Windows.Forms.Label();
            this.timeoutNumeric = new System.Windows.Forms.NumericUpDown();
            this.volumeNameLabel = new System.Windows.Forms.Label();
            this.volumeNameTextBox = new System.Windows.Forms.TextBox();
            this.volumeSerialLabel = new System.Windows.Forms.Label();
            this.volumeSerialTextBox = new System.Windows.Forms.TextBox();
            this.networkUncLabel = new System.Windows.Forms.Label();
            this.networkUncTextBox = new System.Windows.Forms.TextBox();
            this.allocationUnitLabel = new System.Windows.Forms.Label();
            this.allocationUnitNumeric = new System.Windows.Forms.NumericUpDown();
            this.sectorSizeLabel = new System.Windows.Forms.Label();
            this.sectorSizeNumeric = new System.Windows.Forms.NumericUpDown();
            
            // Command preview group
            this.commandPreviewGroup = new System.Windows.Forms.GroupBox();
            this.commandPreviewTextBox = new System.Windows.Forms.TextBox();
            this.copyCommandButton = new System.Windows.Forms.Button();
            
            // Suspend layout
            this.mainContainer.SuspendLayout();
            this.driveGroup.SuspendLayout();
            this.directoryGroup.SuspendLayout();
            this.basicOptionsGroup.SuspendLayout();
            this.buttonsPanel.SuspendLayout();
            this.advancedModePanel.SuspendLayout();
            this.advancedOptionsGroup.SuspendLayout();
            this.advancedSettingsGroup.SuspendLayout();
            this.commandPreviewGroup.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.timeoutNumeric)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.allocationUnitNumeric)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.sectorSizeNumeric)).BeginInit();
            this.SuspendLayout();
            
            // 
            // mainContainer
            // 
            this.mainContainer.AutoSize = true;
            this.mainContainer.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
            this.mainContainer.FlowDirection = System.Windows.Forms.FlowDirection.TopDown;
            this.mainContainer.Location = new System.Drawing.Point(5, 5);
            this.mainContainer.Name = "mainContainer";
            this.mainContainer.Padding = new System.Windows.Forms.Padding(5);
            this.mainContainer.WrapContents = false;
            this.mainContainer.Controls.Add(this.driveGroup);
            this.mainContainer.Controls.Add(this.directoryGroup);
            this.mainContainer.Controls.Add(this.basicOptionsGroup);
            this.mainContainer.Controls.Add(this.buttonsPanel);
            this.mainContainer.Controls.Add(this.advancedModePanel);
            this.mainContainer.Controls.Add(this.advancedOptionsGroup);
            this.mainContainer.Controls.Add(this.advancedSettingsGroup);
            this.mainContainer.Controls.Add(this.commandPreviewGroup);
            this.mainContainer.TabIndex = 0;
            
            // 
            // driveGroup
            // 
            this.driveGroup.Controls.Add(this.driveListView);
            this.driveGroup.Controls.Add(this.refreshButton);
            this.driveGroup.Margin = new System.Windows.Forms.Padding(5);
            this.driveGroup.Name = "driveGroup";
            this.driveGroup.Size = new System.Drawing.Size(440, 140);
            this.driveGroup.TabIndex = 0;
            this.driveGroup.TabStop = false;
            this.driveGroup.Text = "Drive Selection";
            
            // 
            // driveListView
            // 
            this.driveListView.Columns.AddRange(new System.Windows.Forms.ColumnHeader[] {
                this.driveColumn,
                this.statusColumn});
            this.driveListView.FullRowSelect = true;
            this.driveListView.GridLines = true;
            this.driveListView.HideSelection = false;
            this.driveListView.Location = new System.Drawing.Point(10, 20);
            this.driveListView.MultiSelect = false;
            this.driveListView.Name = "driveListView";
            this.driveListView.Size = new System.Drawing.Size(340, 110);
            this.driveListView.TabIndex = 0;
            this.driveListView.UseCompatibleStateImageBehavior = false;
            this.driveListView.View = System.Windows.Forms.View.Details;
            this.driveListView.SelectedIndexChanged += new System.EventHandler(this.driveListView_SelectedIndexChanged);
            
            // 
            // driveColumn
            // 
            this.driveColumn.Text = "Drive";
            this.driveColumn.Width = 60;
            
            // 
            // statusColumn
            // 
            this.statusColumn.Text = "Status";
            this.statusColumn.Width = 260;
            
            // 
            // refreshButton
            // 
            this.refreshButton.Location = new System.Drawing.Point(360, 20);
            this.refreshButton.Name = "refreshButton";
            this.refreshButton.Size = new System.Drawing.Size(70, 28);
            this.refreshButton.TabIndex = 1;
            this.refreshButton.Text = "Refresh";
            this.refreshButton.UseVisualStyleBackColor = true;
            this.refreshButton.Click += new System.EventHandler(this.refreshButton_Click);
            this.toolTip.SetToolTip(this.refreshButton, "Refresh drive list");
            
            // 
            // directoryGroup
            // 
            this.directoryGroup.Controls.Add(this.rootPathCombo);
            this.directoryGroup.Controls.Add(this.selectDirectoryButton);
            this.directoryGroup.Margin = new System.Windows.Forms.Padding(5);
            this.directoryGroup.Name = "directoryGroup";
            this.directoryGroup.Size = new System.Drawing.Size(440, 55);
            this.directoryGroup.TabIndex = 1;
            this.directoryGroup.TabStop = false;
            this.directoryGroup.Text = "Encrypted Directory (rootDir)";
            
            // 
            // rootPathCombo
            // 
            this.rootPathCombo.FormattingEnabled = true;
            this.rootPathCombo.Location = new System.Drawing.Point(10, 22);
            this.rootPathCombo.Name = "rootPathCombo";
            this.rootPathCombo.Size = new System.Drawing.Size(330, 21);
            this.rootPathCombo.TabIndex = 0;
            this.rootPathCombo.TextChanged += new System.EventHandler(this.rootPathCombo_TextChanged);
            this.toolTip.SetToolTip(this.rootPathCombo, "Path to the encrypted directory (rootDir)");
            
            // 
            // selectDirectoryButton
            // 
            this.selectDirectoryButton.Location = new System.Drawing.Point(350, 20);
            this.selectDirectoryButton.Name = "selectDirectoryButton";
            this.selectDirectoryButton.Size = new System.Drawing.Size(80, 25);
            this.selectDirectoryButton.TabIndex = 1;
            this.selectDirectoryButton.Text = "Browse...";
            this.selectDirectoryButton.UseVisualStyleBackColor = true;
            this.selectDirectoryButton.Click += new System.EventHandler(this.selectDirectoryButton_Click);
            
            // 
            // basicOptionsGroup
            // 
            this.basicOptionsGroup.Controls.Add(this.altStreamCheckBox);
            this.basicOptionsGroup.Controls.Add(this.mountManagerCheckBox);
            this.basicOptionsGroup.Controls.Add(this.caseInsensitiveCheckBox);
            this.basicOptionsGroup.Controls.Add(this.readOnlyCheckBox);
            this.basicOptionsGroup.Controls.Add(this.reverseCheckBox);
            this.basicOptionsGroup.Margin = new System.Windows.Forms.Padding(5);
            this.basicOptionsGroup.Name = "basicOptionsGroup";
            this.basicOptionsGroup.Size = new System.Drawing.Size(440, 55);
            this.basicOptionsGroup.TabIndex = 2;
            this.basicOptionsGroup.TabStop = false;
            this.basicOptionsGroup.Text = "Options";
            
            // 
            // altStreamCheckBox
            // 
            this.altStreamCheckBox.AutoSize = true;
            this.altStreamCheckBox.Checked = true;
            this.altStreamCheckBox.CheckState = System.Windows.Forms.CheckState.Checked;
            this.altStreamCheckBox.Location = new System.Drawing.Point(10, 22);
            this.altStreamCheckBox.Name = "altStreamCheckBox";
            this.altStreamCheckBox.Size = new System.Drawing.Size(76, 17);
            this.altStreamCheckBox.TabIndex = 0;
            this.altStreamCheckBox.Text = "Alt Stream";
            this.altStreamCheckBox.UseVisualStyleBackColor = true;
            this.altStreamCheckBox.CheckedChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.altStreamCheckBox, "Enable NTFS alternate data streams");
            
            // 
            // mountManagerCheckBox
            // 
            this.mountManagerCheckBox.AutoSize = true;
            this.mountManagerCheckBox.Checked = true;
            this.mountManagerCheckBox.CheckState = System.Windows.Forms.CheckState.Checked;
            this.mountManagerCheckBox.Location = new System.Drawing.Point(100, 22);
            this.mountManagerCheckBox.Name = "mountManagerCheckBox";
            this.mountManagerCheckBox.Size = new System.Drawing.Size(101, 17);
            this.mountManagerCheckBox.TabIndex = 1;
            this.mountManagerCheckBox.Text = "Mount Manager";
            this.mountManagerCheckBox.UseVisualStyleBackColor = true;
            this.mountManagerCheckBox.CheckedChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.mountManagerCheckBox, "Register with Windows Mount Manager (enables Recycle Bin)");
            
            // 
            // caseInsensitiveCheckBox
            // 
            this.caseInsensitiveCheckBox.AutoSize = true;
            this.caseInsensitiveCheckBox.Checked = true;
            this.caseInsensitiveCheckBox.CheckState = System.Windows.Forms.CheckState.Checked;
            this.caseInsensitiveCheckBox.Location = new System.Drawing.Point(220, 22);
            this.caseInsensitiveCheckBox.Name = "caseInsensitiveCheckBox";
            this.caseInsensitiveCheckBox.Size = new System.Drawing.Size(83, 17);
            this.caseInsensitiveCheckBox.TabIndex = 2;
            this.caseInsensitiveCheckBox.Text = "Ignore Case";
            this.caseInsensitiveCheckBox.UseVisualStyleBackColor = true;
            this.caseInsensitiveCheckBox.CheckedChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.caseInsensitiveCheckBox, "Case-insensitive filename matching");
            
            // 
            // readOnlyCheckBox
            // 
            this.readOnlyCheckBox.AutoSize = true;
            this.readOnlyCheckBox.Location = new System.Drawing.Point(320, 22);
            this.readOnlyCheckBox.Name = "readOnlyCheckBox";
            this.readOnlyCheckBox.Size = new System.Drawing.Size(76, 17);
            this.readOnlyCheckBox.TabIndex = 3;
            this.readOnlyCheckBox.Text = "Read Only";
            this.readOnlyCheckBox.UseVisualStyleBackColor = true;
            this.readOnlyCheckBox.CheckedChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.readOnlyCheckBox, "Mount as read-only filesystem");
            
            // 
            // reverseCheckBox
            // 
            this.reverseCheckBox.AutoSize = true;
            this.reverseCheckBox.Location = new System.Drawing.Point(10, 22);
            this.reverseCheckBox.Name = "reverseCheckBox";
            this.reverseCheckBox.Size = new System.Drawing.Size(66, 17);
            this.reverseCheckBox.TabIndex = 4;
            this.reverseCheckBox.Text = "Reverse";
            this.reverseCheckBox.UseVisualStyleBackColor = true;
            this.reverseCheckBox.Visible = false;
            this.reverseCheckBox.CheckedChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.reverseCheckBox, "Reverse mode: show plaintext as encrypted");
            
            // 
            // buttonsPanel
            // 
            this.buttonsPanel.Controls.Add(this.mountButton);
            this.buttonsPanel.Controls.Add(this.unmountButton);
            this.buttonsPanel.Margin = new System.Windows.Forms.Padding(5);
            this.buttonsPanel.Name = "buttonsPanel";
            this.buttonsPanel.Size = new System.Drawing.Size(440, 45);
            this.buttonsPanel.TabIndex = 3;
            
            // 
            // mountButton
            // 
            this.mountButton.Enabled = false;
            this.mountButton.Font = new System.Drawing.Font("Microsoft Sans Serif", 9F, System.Drawing.FontStyle.Bold);
            this.mountButton.Location = new System.Drawing.Point(0, 5);
            this.mountButton.Name = "mountButton";
            this.mountButton.Size = new System.Drawing.Size(210, 35);
            this.mountButton.TabIndex = 0;
            this.mountButton.Text = "Mount";
            this.mountButton.UseVisualStyleBackColor = true;
            this.mountButton.Click += new System.EventHandler(this.mountButton_Click);
            
            // 
            // unmountButton
            // 
            this.unmountButton.Enabled = false;
            this.unmountButton.Font = new System.Drawing.Font("Microsoft Sans Serif", 9F);
            this.unmountButton.Location = new System.Drawing.Point(225, 5);
            this.unmountButton.Name = "unmountButton";
            this.unmountButton.Size = new System.Drawing.Size(210, 35);
            this.unmountButton.TabIndex = 1;
            this.unmountButton.Text = "Unmount";
            this.unmountButton.UseVisualStyleBackColor = true;
            this.unmountButton.Click += new System.EventHandler(this.unmountButton_Click);
            
            // 
            // advancedModePanel
            // 
            this.advancedModePanel.Controls.Add(this.separatorLabel);
            this.advancedModePanel.Controls.Add(this.advancedModeCheckBox);
            this.advancedModePanel.Margin = new System.Windows.Forms.Padding(5, 10, 5, 5);
            this.advancedModePanel.Name = "advancedModePanel";
            this.advancedModePanel.Size = new System.Drawing.Size(440, 25);
            this.advancedModePanel.TabIndex = 4;
            
            // 
            // separatorLabel
            // 
            this.separatorLabel.BorderStyle = System.Windows.Forms.BorderStyle.Fixed3D;
            this.separatorLabel.Location = new System.Drawing.Point(0, 0);
            this.separatorLabel.Name = "separatorLabel";
            this.separatorLabel.Size = new System.Drawing.Size(440, 2);
            this.separatorLabel.TabIndex = 0;
            
            // 
            // advancedModeCheckBox
            // 
            this.advancedModeCheckBox.AutoSize = true;
            this.advancedModeCheckBox.Location = new System.Drawing.Point(5, 8);
            this.advancedModeCheckBox.Name = "advancedModeCheckBox";
            this.advancedModeCheckBox.Size = new System.Drawing.Size(166, 17);
            this.advancedModeCheckBox.TabIndex = 1;
            this.advancedModeCheckBox.Text = "Show Advanced Options Бе";
            this.advancedModeCheckBox.UseVisualStyleBackColor = true;
            this.advancedModeCheckBox.CheckedChanged += new System.EventHandler(this.advancedModeCheckBox_CheckedChanged);
            this.toolTip.SetToolTip(this.advancedModeCheckBox, "Show/hide advanced options and command preview");
            
            // 
            // advancedOptionsGroup
            // 
            this.advancedOptionsGroup.Controls.Add(this.paranoiaCheckBox);
            this.advancedOptionsGroup.Controls.Add(this.removableCheckBox);
            this.advancedOptionsGroup.Controls.Add(this.currentSessionCheckBox);
            this.advancedOptionsGroup.Controls.Add(this.fileLockUserModeCheckBox);
            this.advancedOptionsGroup.Controls.Add(this.enableUnmountNetworkCheckBox);
            this.advancedOptionsGroup.Controls.Add(this.allowIpcBatchingCheckBox);
            this.advancedOptionsGroup.Controls.Add(this.debugModeCheckBox);
            this.advancedOptionsGroup.Controls.Add(this.stderrCheckBox);
            this.advancedOptionsGroup.Controls.Add(this.reverseCheckBox);
            this.advancedOptionsGroup.Margin = new System.Windows.Forms.Padding(5);
            this.advancedOptionsGroup.Name = "advancedOptionsGroup";
            this.advancedOptionsGroup.Size = new System.Drawing.Size(440, 75);
            this.advancedOptionsGroup.TabIndex = 5;
            this.advancedOptionsGroup.TabStop = false;
            this.advancedOptionsGroup.Text = "Advanced Options";
            this.advancedOptionsGroup.Visible = false;
            
            // 
            // paranoiaCheckBox
            // 
            this.paranoiaCheckBox.AutoSize = true;
            this.paranoiaCheckBox.Location = new System.Drawing.Point(10, 20);
            this.paranoiaCheckBox.Name = "paranoiaCheckBox";
            this.paranoiaCheckBox.Size = new System.Drawing.Size(68, 17);
            this.paranoiaCheckBox.TabIndex = 0;
            this.paranoiaCheckBox.Text = "Paranoia";
            this.paranoiaCheckBox.UseVisualStyleBackColor = true;
            this.paranoiaCheckBox.CheckedChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.paranoiaCheckBox, "AES-256, renamed IVs, external IV chaining");
            
            // 
            // removableCheckBox
            // 
            this.removableCheckBox.AutoSize = true;
            this.removableCheckBox.Location = new System.Drawing.Point(95, 20);
            this.removableCheckBox.Name = "removableCheckBox";
            this.removableCheckBox.Size = new System.Drawing.Size(80, 17);
            this.removableCheckBox.TabIndex = 1;
            this.removableCheckBox.Text = "Removable";
            this.removableCheckBox.UseVisualStyleBackColor = true;
            this.removableCheckBox.CheckedChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.removableCheckBox, "Present volume as removable media");
            
            // 
            // currentSessionCheckBox
            // 
            this.currentSessionCheckBox.AutoSize = true;
            this.currentSessionCheckBox.Location = new System.Drawing.Point(190, 20);
            this.currentSessionCheckBox.Name = "currentSessionCheckBox";
            this.currentSessionCheckBox.Size = new System.Drawing.Size(100, 17);
            this.currentSessionCheckBox.TabIndex = 2;
            this.currentSessionCheckBox.Text = "Current Session";
            this.currentSessionCheckBox.UseVisualStyleBackColor = true;
            this.currentSessionCheckBox.CheckedChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.currentSessionCheckBox, "Make volume visible only in current session");
            
            // 
            // fileLockUserModeCheckBox
            // 
            this.fileLockUserModeCheckBox.AutoSize = true;
            this.fileLockUserModeCheckBox.Location = new System.Drawing.Point(305, 20);
            this.fileLockUserModeCheckBox.Name = "fileLockUserModeCheckBox";
            this.fileLockUserModeCheckBox.Size = new System.Drawing.Size(95, 17);
            this.fileLockUserModeCheckBox.TabIndex = 3;
            this.fileLockUserModeCheckBox.Text = "FileLock User";
            this.fileLockUserModeCheckBox.UseVisualStyleBackColor = true;
            this.fileLockUserModeCheckBox.CheckedChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.fileLockUserModeCheckBox, "Handle LockFile/UnlockFile in user mode");
            
            // 
            // enableUnmountNetworkCheckBox
            // 
            this.enableUnmountNetworkCheckBox.AutoSize = true;
            this.enableUnmountNetworkCheckBox.Location = new System.Drawing.Point(10, 45);
            this.enableUnmountNetworkCheckBox.Name = "enableUnmountNetworkCheckBox";
            this.enableUnmountNetworkCheckBox.Size = new System.Drawing.Size(125, 17);
            this.enableUnmountNetworkCheckBox.TabIndex = 4;
            this.enableUnmountNetworkCheckBox.Text = "Unmount via Explorer";
            this.enableUnmountNetworkCheckBox.UseVisualStyleBackColor = true;
            this.enableUnmountNetworkCheckBox.CheckedChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.enableUnmountNetworkCheckBox, "Allow unmounting network drive via Explorer");
            
            // 
            // allowIpcBatchingCheckBox
            // 
            this.allowIpcBatchingCheckBox.AutoSize = true;
            this.allowIpcBatchingCheckBox.Location = new System.Drawing.Point(150, 45);
            this.allowIpcBatchingCheckBox.Name = "allowIpcBatchingCheckBox";
            this.allowIpcBatchingCheckBox.Size = new System.Drawing.Size(90, 17);
            this.allowIpcBatchingCheckBox.TabIndex = 5;
            this.allowIpcBatchingCheckBox.Text = "IPC Batching";
            this.allowIpcBatchingCheckBox.UseVisualStyleBackColor = true;
            this.allowIpcBatchingCheckBox.CheckedChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.allowIpcBatchingCheckBox, "Enable IPC batching for slow filesystems");
            
            // 
            // debugModeCheckBox
            // 
            this.debugModeCheckBox.AutoSize = true;
            this.debugModeCheckBox.Location = new System.Drawing.Point(260, 45);
            this.debugModeCheckBox.Name = "debugModeCheckBox";
            this.debugModeCheckBox.Size = new System.Drawing.Size(58, 17);
            this.debugModeCheckBox.TabIndex = 6;
            this.debugModeCheckBox.Text = "Debug";
            this.debugModeCheckBox.UseVisualStyleBackColor = true;
            this.debugModeCheckBox.CheckedChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.debugModeCheckBox, "Send debug output to debugger");
            
            // 
            // stderrCheckBox
            // 
            this.stderrCheckBox.AutoSize = true;
            this.stderrCheckBox.Location = new System.Drawing.Point(335, 45);
            this.stderrCheckBox.Name = "stderrCheckBox";
            this.stderrCheckBox.Size = new System.Drawing.Size(55, 17);
            this.stderrCheckBox.TabIndex = 7;
            this.stderrCheckBox.Text = "Stderr";
            this.stderrCheckBox.UseVisualStyleBackColor = true;
            this.stderrCheckBox.CheckedChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.stderrCheckBox, "Send debug output to stderr");
            
            // 
            // advancedSettingsGroup
            // 
            this.advancedSettingsGroup.Controls.Add(this.timeoutLabel);
            this.advancedSettingsGroup.Controls.Add(this.timeoutNumeric);
            this.advancedSettingsGroup.Controls.Add(this.volumeNameLabel);
            this.advancedSettingsGroup.Controls.Add(this.volumeNameTextBox);
            this.advancedSettingsGroup.Controls.Add(this.volumeSerialLabel);
            this.advancedSettingsGroup.Controls.Add(this.volumeSerialTextBox);
            this.advancedSettingsGroup.Controls.Add(this.networkUncLabel);
            this.advancedSettingsGroup.Controls.Add(this.networkUncTextBox);
            this.advancedSettingsGroup.Controls.Add(this.allocationUnitLabel);
            this.advancedSettingsGroup.Controls.Add(this.allocationUnitNumeric);
            this.advancedSettingsGroup.Controls.Add(this.sectorSizeLabel);
            this.advancedSettingsGroup.Controls.Add(this.sectorSizeNumeric);
            this.advancedSettingsGroup.Margin = new System.Windows.Forms.Padding(5);
            this.advancedSettingsGroup.Name = "advancedSettingsGroup";
            this.advancedSettingsGroup.Size = new System.Drawing.Size(440, 75);
            this.advancedSettingsGroup.TabIndex = 6;
            this.advancedSettingsGroup.TabStop = false;
            this.advancedSettingsGroup.Text = "Settings";
            this.advancedSettingsGroup.Visible = false;
            
            // 
            // timeoutLabel
            // 
            this.timeoutLabel.AutoSize = true;
            this.timeoutLabel.Location = new System.Drawing.Point(7, 22);
            this.timeoutLabel.Name = "timeoutLabel";
            this.timeoutLabel.Size = new System.Drawing.Size(48, 13);
            this.timeoutLabel.TabIndex = 0;
            this.timeoutLabel.Text = "Timeout:";
            
            // 
            // timeoutNumeric
            // 
            this.timeoutNumeric.Location = new System.Drawing.Point(60, 20);
            this.timeoutNumeric.Maximum = new decimal(new int[] { 600000, 0, 0, 0 });
            this.timeoutNumeric.Minimum = new decimal(new int[] { 1000, 0, 0, 0 });
            this.timeoutNumeric.Name = "timeoutNumeric";
            this.timeoutNumeric.Size = new System.Drawing.Size(70, 20);
            this.timeoutNumeric.TabIndex = 1;
            this.timeoutNumeric.Value = new decimal(new int[] { 120000, 0, 0, 0 });
            this.timeoutNumeric.ValueChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.timeoutNumeric, "Timeout in milliseconds (default: 120000)");
            
            // 
            // volumeNameLabel
            // 
            this.volumeNameLabel.AutoSize = true;
            this.volumeNameLabel.Location = new System.Drawing.Point(140, 22);
            this.volumeNameLabel.Name = "volumeNameLabel";
            this.volumeNameLabel.Size = new System.Drawing.Size(38, 13);
            this.volumeNameLabel.TabIndex = 2;
            this.volumeNameLabel.Text = "Name:";
            
            // 
            // volumeNameTextBox
            // 
            this.volumeNameTextBox.Location = new System.Drawing.Point(182, 19);
            this.volumeNameTextBox.MaxLength = 31;
            this.volumeNameTextBox.Name = "volumeNameTextBox";
            this.volumeNameTextBox.Size = new System.Drawing.Size(90, 20);
            this.volumeNameTextBox.TabIndex = 3;
            this.volumeNameTextBox.TextChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.volumeNameTextBox, "Volume name in Explorer (max 31 chars)");
            
            // 
            // volumeSerialLabel
            // 
            this.volumeSerialLabel.AutoSize = true;
            this.volumeSerialLabel.Location = new System.Drawing.Point(285, 22);
            this.volumeSerialLabel.Name = "volumeSerialLabel";
            this.volumeSerialLabel.Size = new System.Drawing.Size(36, 13);
            this.volumeSerialLabel.TabIndex = 4;
            this.volumeSerialLabel.Text = "Serial:";
            
            // 
            // volumeSerialTextBox
            // 
            this.volumeSerialTextBox.Location = new System.Drawing.Point(325, 19);
            this.volumeSerialTextBox.MaxLength = 8;
            this.volumeSerialTextBox.Name = "volumeSerialTextBox";
            this.volumeSerialTextBox.Size = new System.Drawing.Size(75, 20);
            this.volumeSerialTextBox.TabIndex = 5;
            this.volumeSerialTextBox.TextChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.volumeSerialTextBox, "Volume serial number in hex (e.g., DEADBEEF)");
            
            // 
            // networkUncLabel
            // 
            this.networkUncLabel.AutoSize = true;
            this.networkUncLabel.Location = new System.Drawing.Point(7, 47);
            this.networkUncLabel.Name = "networkUncLabel";
            this.networkUncLabel.Size = new System.Drawing.Size(34, 13);
            this.networkUncLabel.TabIndex = 6;
            this.networkUncLabel.Text = "UNC:";
            
            // 
            // networkUncTextBox
            // 
            this.networkUncTextBox.Location = new System.Drawing.Point(45, 44);
            this.networkUncTextBox.Name = "networkUncTextBox";
            this.networkUncTextBox.Size = new System.Drawing.Size(150, 20);
            this.networkUncTextBox.TabIndex = 7;
            this.networkUncTextBox.TextChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.networkUncTextBox, "Network UNC path (e.g., \\\\server\\share)");
            
            // 
            // allocationUnitLabel
            // 
            this.allocationUnitLabel.AutoSize = true;
            this.allocationUnitLabel.Location = new System.Drawing.Point(210, 47);
            this.allocationUnitLabel.Name = "allocationUnitLabel";
            this.allocationUnitLabel.Size = new System.Drawing.Size(55, 13);
            this.allocationUnitLabel.TabIndex = 8;
            this.allocationUnitLabel.Text = "AllocUnit:";
            
            // 
            // allocationUnitNumeric
            // 
            this.allocationUnitNumeric.Location = new System.Drawing.Point(270, 44);
            this.allocationUnitNumeric.Maximum = new decimal(new int[] { 65536, 0, 0, 0 });
            this.allocationUnitNumeric.Name = "allocationUnitNumeric";
            this.allocationUnitNumeric.Size = new System.Drawing.Size(60, 20);
            this.allocationUnitNumeric.TabIndex = 9;
            this.allocationUnitNumeric.ValueChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.allocationUnitNumeric, "Allocation unit size (0 = default)");
            
            // 
            // sectorSizeLabel
            // 
            this.sectorSizeLabel.AutoSize = true;
            this.sectorSizeLabel.Location = new System.Drawing.Point(345, 47);
            this.sectorSizeLabel.Name = "sectorSizeLabel";
            this.sectorSizeLabel.Size = new System.Drawing.Size(41, 13);
            this.sectorSizeLabel.TabIndex = 10;
            this.sectorSizeLabel.Text = "Sector:";
            
            // 
            // sectorSizeNumeric
            // 
            this.sectorSizeNumeric.Location = new System.Drawing.Point(390, 44);
            this.sectorSizeNumeric.Maximum = new decimal(new int[] { 65536, 0, 0, 0 });
            this.sectorSizeNumeric.Name = "sectorSizeNumeric";
            this.sectorSizeNumeric.Size = new System.Drawing.Size(40, 20);
            this.sectorSizeNumeric.TabIndex = 11;
            this.sectorSizeNumeric.ValueChanged += new System.EventHandler(this.anyOption_Changed);
            this.toolTip.SetToolTip(this.sectorSizeNumeric, "Sector size (0 = default)");
            
            // 
            // commandPreviewGroup
            // 
            this.commandPreviewGroup.Controls.Add(this.commandPreviewTextBox);
            this.commandPreviewGroup.Controls.Add(this.copyCommandButton);
            this.commandPreviewGroup.Margin = new System.Windows.Forms.Padding(5);
            this.commandPreviewGroup.Name = "commandPreviewGroup";
            this.commandPreviewGroup.Size = new System.Drawing.Size(440, 75);
            this.commandPreviewGroup.TabIndex = 7;
            this.commandPreviewGroup.TabStop = false;
            this.commandPreviewGroup.Text = "Command Preview";
            this.commandPreviewGroup.Visible = false;
            
            // 
            // commandPreviewTextBox
            // 
            this.commandPreviewTextBox.BackColor = System.Drawing.Color.FromArgb(((int)(((byte)(30)))), ((int)(((byte)(30)))), ((int)(((byte)(30)))));
            this.commandPreviewTextBox.Font = new System.Drawing.Font("Consolas", 8.25F);
            this.commandPreviewTextBox.ForeColor = System.Drawing.Color.LightGreen;
            this.commandPreviewTextBox.Location = new System.Drawing.Point(10, 20);
            this.commandPreviewTextBox.Multiline = true;
            this.commandPreviewTextBox.Name = "commandPreviewTextBox";
            this.commandPreviewTextBox.ReadOnly = true;
            this.commandPreviewTextBox.ScrollBars = System.Windows.Forms.ScrollBars.Horizontal;
            this.commandPreviewTextBox.Size = new System.Drawing.Size(350, 45);
            this.commandPreviewTextBox.TabIndex = 0;
            this.commandPreviewTextBox.WordWrap = false;
            
            // 
            // copyCommandButton
            // 
            this.copyCommandButton.Location = new System.Drawing.Point(370, 25);
            this.copyCommandButton.Name = "copyCommandButton";
            this.copyCommandButton.Size = new System.Drawing.Size(60, 35);
            this.copyCommandButton.TabIndex = 1;
            this.copyCommandButton.Text = "Copy";
            this.copyCommandButton.UseVisualStyleBackColor = true;
            this.copyCommandButton.Click += new System.EventHandler(this.copyCommandButton_Click);
            this.toolTip.SetToolTip(this.copyCommandButton, "Copy command to clipboard");
            
            // 
            // MainForm
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.AutoSize = true;
            this.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
            this.Controls.Add(this.mainContainer);
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedSingle;
            this.Icon = ((System.Drawing.Icon)(resources.GetObject("$this.Icon")));
            this.MaximizeBox = false;
            this.Name = "MainForm";
            this.Padding = new System.Windows.Forms.Padding(5);
            this.StartPosition = System.Windows.Forms.FormStartPosition.CenterScreen;
            this.Text = "EncFSy";
            this.Load += new System.EventHandler(this.MainForm_Load);
            
            // Resume layout
            this.mainContainer.ResumeLayout(false);
            this.driveGroup.ResumeLayout(false);
            this.directoryGroup.ResumeLayout(false);
            this.basicOptionsGroup.ResumeLayout(false);
            this.basicOptionsGroup.PerformLayout();
            this.buttonsPanel.ResumeLayout(false);
            this.advancedModePanel.ResumeLayout(false);
            this.advancedModePanel.PerformLayout();
            this.advancedOptionsGroup.ResumeLayout(false);
            this.advancedOptionsGroup.PerformLayout();
            this.advancedSettingsGroup.ResumeLayout(false);
            this.advancedSettingsGroup.PerformLayout();
            this.commandPreviewGroup.ResumeLayout(false);
            this.commandPreviewGroup.PerformLayout();
            ((System.ComponentModel.ISupportInitialize)(this.timeoutNumeric)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.allocationUnitNumeric)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.sectorSizeNumeric)).EndInit();
            this.ResumeLayout(false);
            this.PerformLayout();
        }

        #endregion

        // Main container
        private System.Windows.Forms.FlowLayoutPanel mainContainer;
        
        // Groups
        private System.Windows.Forms.GroupBox driveGroup;
        private System.Windows.Forms.GroupBox directoryGroup;
        private System.Windows.Forms.GroupBox basicOptionsGroup;
        private System.Windows.Forms.Panel buttonsPanel;
        private System.Windows.Forms.Panel advancedModePanel;
        private System.Windows.Forms.Label separatorLabel;
        private System.Windows.Forms.GroupBox advancedOptionsGroup;
        private System.Windows.Forms.GroupBox advancedSettingsGroup;
        private System.Windows.Forms.GroupBox commandPreviewGroup;
        
        // Drive selection
        private System.Windows.Forms.ListView driveListView;
        private System.Windows.Forms.ColumnHeader driveColumn;
        private System.Windows.Forms.ColumnHeader statusColumn;
        private System.Windows.Forms.Button refreshButton;
        
        // Directory selection
        private System.Windows.Forms.ComboBox rootPathCombo;
        private System.Windows.Forms.Button selectDirectoryButton;
        
        // Basic options
        private System.Windows.Forms.CheckBox altStreamCheckBox;
        private System.Windows.Forms.CheckBox mountManagerCheckBox;
        private System.Windows.Forms.CheckBox caseInsensitiveCheckBox;
        private System.Windows.Forms.CheckBox readOnlyCheckBox;
        private System.Windows.Forms.CheckBox reverseCheckBox;
        
        // Advanced mode toggle
        private System.Windows.Forms.CheckBox advancedModeCheckBox;
        
        // Action buttons
        private System.Windows.Forms.Button mountButton;
        private System.Windows.Forms.Button unmountButton;
        
        // Advanced options
        private System.Windows.Forms.CheckBox paranoiaCheckBox;
        private System.Windows.Forms.CheckBox removableCheckBox;
        private System.Windows.Forms.CheckBox currentSessionCheckBox;
        private System.Windows.Forms.CheckBox fileLockUserModeCheckBox;
        private System.Windows.Forms.CheckBox enableUnmountNetworkCheckBox;
        private System.Windows.Forms.CheckBox allowIpcBatchingCheckBox;
        private System.Windows.Forms.CheckBox debugModeCheckBox;
        private System.Windows.Forms.CheckBox stderrCheckBox;
        
        // Advanced settings
        private System.Windows.Forms.Label timeoutLabel;
        private System.Windows.Forms.NumericUpDown timeoutNumeric;
        private System.Windows.Forms.Label volumeNameLabel;
        private System.Windows.Forms.TextBox volumeNameTextBox;
        private System.Windows.Forms.Label volumeSerialLabel;
        private System.Windows.Forms.TextBox volumeSerialTextBox;
        private System.Windows.Forms.Label networkUncLabel;
        private System.Windows.Forms.TextBox networkUncTextBox;
        private System.Windows.Forms.Label allocationUnitLabel;
        private System.Windows.Forms.NumericUpDown allocationUnitNumeric;
        private System.Windows.Forms.Label sectorSizeLabel;
        private System.Windows.Forms.NumericUpDown sectorSizeNumeric;
        
        // Command preview
        private System.Windows.Forms.TextBox commandPreviewTextBox;
        private System.Windows.Forms.Button copyCommandButton;
        
        // Tooltip
        private System.Windows.Forms.ToolTip toolTip;
    }
}

