using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Threading;
using System.Diagnostics;
using System.IO;

// This is the code for your desktop app.
// Press Ctrl+F5 (or go to Debug > Start Without Debugging) to run your app.

namespace EncFSy_gui
{
    public partial class MainForm : Form
    {
        string historyFile = Application.LocalUserAppDataPath + "\\history.txt";
        string encfsExecutable = System.IO.Path.GetDirectoryName(Application.ExecutablePath) + "\\encfs.exe";

        public MainForm()
        {
             InitializeComponent();
        }

        private void selectDirectory_Click(object sender, EventArgs e)
        {
            FolderBrowserDialog dialog = new FolderBrowserDialog();
            if (dialog.ShowDialog() == DialogResult.OK)
            {
                this.rootPathCombo.Text = dialog.SelectedPath;
            }
        }

        private Process startEncFS(String args)
        {
            Process process = new Process();
            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.FileName = encfsExecutable;
            startInfo.Arguments = args;
            startInfo.CreateNoWindow = true;
            startInfo.RedirectStandardInput = true;
            startInfo.RedirectStandardOutput = true;
            startInfo.UseShellExecute = false;
            process.StartInfo = startInfo;
            process.Start();
            return process;

        }

        private void mount_Click(object sender, EventArgs e)
        {
            string rootPath = this.rootPathCombo.Text;
            using (StreamWriter writer = new StreamWriter(this.historyFile, false, Encoding.UTF8))
            {
                writer.WriteLine(this.rootPathCombo.Text);
                foreach (string item in this.rootPathCombo.Items)
                {
                    if (item.Equals(this.rootPathCombo.Text))
                    {
                        continue;
                    }
                    writer.WriteLine(item);
                }
            }

            string drive = this.driveListView.SelectedItems[0].Text.Substring(0, 1);

            PasswordForm passwordForm = new PasswordForm();
            passwordForm.StartPosition = FormStartPosition.CenterParent;
            passwordForm.ShowDialog(this);
            passwordForm.Dispose();
            string password = passwordForm.password;
            if (password == null || password.Equals(""))
            {
                return;
            }

            String args = "\"" + rootPath + "\"" + " " + drive;
            if (this.altStreamCheckBox.Checked)
            {
                args += " --alt-stream";
            }
            if (this.reverseCheckBox.Checked)
            {
                args += " --reverse";
            }
            Process process = this.startEncFS(args);

            Thread t1 = new Thread(new ThreadStart(delegate ()
            {
                process.StandardInput.WriteLine(password);
            }));
            t1.Start();

            Thread t2 = new Thread(new ThreadStart(delegate ()
            {
                process.StandardOutput.ReadLine();
                string encfsOut = process.StandardOutput.ReadLine();
                if (encfsOut != null && encfsOut.Equals("Enter new password: "))
                {
                    process.StandardInput.WriteLine(password);
                    encfsOut = process.StandardOutput.ReadLine();
                    encfsOut = process.StandardOutput.ReadLine();
                }
                if (encfsOut != null && !encfsOut.Equals("Success"))
                {
                    MessageBox.Show("'"+encfsOut+ "'");
                }
            }));
            t2.IsBackground = true;
            t2.Start();

            Thread.Sleep(3000);
            this.updateDrives();
        }

        private void unmount_Click(object sender, EventArgs e)
        {
            string drive = this.driveListView.SelectedItems[0].Text.Substring(0, 1);

            Process process = this.startEncFS("-u " + drive);
            process.WaitForExit();
            process.Close();
            this.updateDrives();
        }

        private void quit_Click(object sender, EventArgs e)
        {
            this.Close();
            this.Dispose();
        }

        private void MainForm_Load(object sender, EventArgs e)
        {
            this.updateDrives();
        }

        private void updateDrives()
        {
            this.rootPathCombo.Items.Clear();
            if (File.Exists(this.historyFile))
            {
                using (StreamReader reader = new StreamReader(this.historyFile, Encoding.UTF8))
                {
                    string line;
                    while ((line = reader.ReadLine()) != null)
                    {
                        this.rootPathCombo.Items.Add(line);
                    }
                    if (this.rootPathCombo.Items.Count > 0)
                    {
                        this.rootPathCombo.SelectedIndex = 0;
                    }
                }
            }

            {
                List<string> drives = new List<string>();
                Process process = this.startEncFS("-l");
                string line;
                while ((line = process.StandardOutput.ReadLine()) != null)
                {
                    drives.Add(line.Substring(line.Length - 2));
                }

                this.driveListView.Items.Clear();
                this.driveListView.View = View.Details;
                for (char i = 'A'; i <= 'Z'; ++i)
                {
                    string drive = i + ":";
                    if (Directory.Exists(drive))
                    {
                        if (drives.Contains(drive))
                        {
                            string[] item = { drive, "EncFS" };
                            this.driveListView.Items.Add(new ListViewItem(item));
                        }
                        continue;
                    }
                    else
                    {
                        string[] item = { drive, "" };
                        this.driveListView.Items.Add(new ListViewItem(item));
                    }
                }
            }
            this.updateButtons();
        }

        private void updateButtons()
        {
            if (this.driveListView.SelectedItems.Count == 0)
            {
                this.mountButton.Enabled = false;
                this.unmountButton.Enabled = false;
            }
            else if (this.driveListView.SelectedItems[0].SubItems[1].Text.Equals(""))
            {
                this.mountButton.Enabled = Directory.Exists(this.rootPathCombo.Text);
                this.unmountButton.Enabled = false;
            }
            else
            {
                this.mountButton.Enabled = false;
                this.unmountButton.Enabled = true;
            }
        }

        private void driveListView_SelectedIndexChanged(object sender, EventArgs e)
        {
            this.updateButtons();
        }

        private void checkBox1_CheckedChanged(object sender, EventArgs e)
        {

        }
    }
}