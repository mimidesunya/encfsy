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
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(MainForm));
            this.selectDirectoryButton = new System.Windows.Forms.Button();
            this.mountButton = new System.Windows.Forms.Button();
            this.unmountButton = new System.Windows.Forms.Button();
            this.quitButton = new System.Windows.Forms.Button();
            this.rootPathCombo = new System.Windows.Forms.ComboBox();
            this.driveListView = new System.Windows.Forms.ListView();
            this.Drive = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
            this.Volume = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
            this.altStreamCheckBox = new System.Windows.Forms.CheckBox();
            this.reverseCheckBox = new System.Windows.Forms.CheckBox();
            this.mountManagerCheckBox = new System.Windows.Forms.CheckBox();
            this.SuspendLayout();
            // 
            // selectDirectoryButton
            // 
            this.selectDirectoryButton.Location = new System.Drawing.Point(221, 185);
            this.selectDirectoryButton.Margin = new System.Windows.Forms.Padding(2);
            this.selectDirectoryButton.Name = "selectDirectoryButton";
            this.selectDirectoryButton.Size = new System.Drawing.Size(97, 20);
            this.selectDirectoryButton.TabIndex = 2;
            this.selectDirectoryButton.Text = "Select DirectiryÅc";
            this.selectDirectoryButton.UseVisualStyleBackColor = true;
            this.selectDirectoryButton.Click += new System.EventHandler(this.selectDirectory_Click);
            // 
            // mountButton
            // 
            this.mountButton.Location = new System.Drawing.Point(12, 237);
            this.mountButton.Name = "mountButton";
            this.mountButton.Size = new System.Drawing.Size(99, 20);
            this.mountButton.TabIndex = 5;
            this.mountButton.Text = "Mount";
            this.mountButton.UseVisualStyleBackColor = true;
            this.mountButton.Click += new System.EventHandler(this.mount_Click);
            // 
            // unmountButton
            // 
            this.unmountButton.Location = new System.Drawing.Point(117, 237);
            this.unmountButton.Name = "unmountButton";
            this.unmountButton.Size = new System.Drawing.Size(99, 20);
            this.unmountButton.TabIndex = 6;
            this.unmountButton.Text = "Unmount";
            this.unmountButton.UseVisualStyleBackColor = true;
            this.unmountButton.Click += new System.EventHandler(this.unmount_Click);
            // 
            // quitButton
            // 
            this.quitButton.Location = new System.Drawing.Point(223, 237);
            this.quitButton.Name = "quitButton";
            this.quitButton.Size = new System.Drawing.Size(95, 20);
            this.quitButton.TabIndex = 7;
            this.quitButton.Text = "Quit";
            this.quitButton.UseVisualStyleBackColor = true;
            this.quitButton.Click += new System.EventHandler(this.quit_Click);
            // 
            // rootPathCombo
            // 
            this.rootPathCombo.FormattingEnabled = true;
            this.rootPathCombo.Location = new System.Drawing.Point(13, 185);
            this.rootPathCombo.Name = "rootPathCombo";
            this.rootPathCombo.Size = new System.Drawing.Size(203, 21);
            this.rootPathCombo.TabIndex = 8;
            // 
            // driveListView
            // 
            this.driveListView.AutoArrange = false;
            this.driveListView.Columns.AddRange(new System.Windows.Forms.ColumnHeader[] {
            this.Drive,
            this.Volume});
            this.driveListView.FullRowSelect = true;
            this.driveListView.HideSelection = false;
            this.driveListView.Location = new System.Drawing.Point(13, 12);
            this.driveListView.Name = "driveListView";
            this.driveListView.Size = new System.Drawing.Size(305, 167);
            this.driveListView.TabIndex = 9;
            this.driveListView.UseCompatibleStateImageBehavior = false;
            this.driveListView.SelectedIndexChanged += new System.EventHandler(this.driveListView_SelectedIndexChanged);
            // 
            // Drive
            // 
            this.Drive.Text = "Drive";
            this.Drive.Width = 40;
            // 
            // Volume
            // 
            this.Volume.Text = "Volume";
            this.Volume.Width = 180;
            // 
            // altStreamCheckBox
            // 
            this.altStreamCheckBox.AutoSize = true;
            this.altStreamCheckBox.Checked = true;
            this.altStreamCheckBox.CheckState = System.Windows.Forms.CheckState.Checked;
            this.altStreamCheckBox.Location = new System.Drawing.Point(13, 214);
            this.altStreamCheckBox.Name = "altStreamCheckBox";
            this.altStreamCheckBox.Size = new System.Drawing.Size(100, 17);
            this.altStreamCheckBox.TabIndex = 10;
            this.altStreamCheckBox.Text = "Alt Data Stream";
            this.altStreamCheckBox.UseVisualStyleBackColor = true;
            this.altStreamCheckBox.CheckedChanged += new System.EventHandler(this.checkBox1_CheckedChanged);
            // 
            // reverseCheckBox
            // 
            this.reverseCheckBox.AutoSize = true;
            this.reverseCheckBox.Location = new System.Drawing.Point(252, 214);
            this.reverseCheckBox.Name = "reverseCheckBox";
            this.reverseCheckBox.Size = new System.Drawing.Size(66, 17);
            this.reverseCheckBox.TabIndex = 11;
            this.reverseCheckBox.Text = "Reverse";
            this.reverseCheckBox.UseVisualStyleBackColor = true;
            // 
            // mountManagerCheckBox
            // 
            this.mountManagerCheckBox.AutoSize = true;
            this.mountManagerCheckBox.Checked = true;
            this.mountManagerCheckBox.CheckState = System.Windows.Forms.CheckState.Checked;
            this.mountManagerCheckBox.Location = new System.Drawing.Point(117, 214);
            this.mountManagerCheckBox.Name = "mountManagerCheckBox";
            this.mountManagerCheckBox.Size = new System.Drawing.Size(101, 17);
            this.mountManagerCheckBox.TabIndex = 12;
            this.mountManagerCheckBox.Text = "Mount Manager";
            this.mountManagerCheckBox.UseVisualStyleBackColor = true;
            this.mountManagerCheckBox.CheckedChanged += new System.EventHandler(this.checkBox1_CheckedChanged_1);
            // 
            // MainForm
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(330, 269);
            this.Controls.Add(this.mountManagerCheckBox);
            this.Controls.Add(this.reverseCheckBox);
            this.Controls.Add(this.altStreamCheckBox);
            this.Controls.Add(this.driveListView);
            this.Controls.Add(this.rootPathCombo);
            this.Controls.Add(this.quitButton);
            this.Controls.Add(this.unmountButton);
            this.Controls.Add(this.mountButton);
            this.Controls.Add(this.selectDirectoryButton);
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedSingle;
            this.Icon = ((System.Drawing.Icon)(resources.GetObject("$this.Icon")));
            this.Margin = new System.Windows.Forms.Padding(2);
            this.MaximizeBox = false;
            this.Name = "MainForm";
            this.Text = "EncFS";
            this.Load += new System.EventHandler(this.MainForm_Load);
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion
        private System.Windows.Forms.Button selectDirectoryButton;
        private System.Windows.Forms.Button mountButton;
        private System.Windows.Forms.Button unmountButton;
        private System.Windows.Forms.Button quitButton;
        private System.Windows.Forms.ComboBox rootPathCombo;
        private System.Windows.Forms.ListView driveListView;
        private System.Windows.Forms.ColumnHeader Drive;
        private System.Windows.Forms.ColumnHeader Volume;
        private System.Windows.Forms.CheckBox altStreamCheckBox;
        private System.Windows.Forms.CheckBox reverseCheckBox;
        private System.Windows.Forms.CheckBox mountManagerCheckBox;
    }
}

