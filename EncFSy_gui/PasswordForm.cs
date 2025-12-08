using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace EncFSy_gui
{
    public partial class PasswordForm : Form
    {
        public string Password { get; private set; }

        public PasswordForm()
        {
            InitializeComponent();
            this.AcceptButton = this.okButton;
            this.CancelButton = this.cancelButton;
        }

        private void okButton_Click(object sender, EventArgs e)
        {
            this.Password = this.passwordText.Text;
            this.DialogResult = DialogResult.OK;
            this.Close();
        }

        private void cancelButton_Click(object sender, EventArgs e)
        {
            this.Password = null;
            this.DialogResult = DialogResult.Cancel;
            this.Close();
        }

        private void showPassword_CheckedChanged(object sender, EventArgs e)
        {
            this.passwordText.PasswordChar = this.showPassword.Checked ? '\0' : '●';
        }

        private void PasswordForm_Shown(object sender, EventArgs e)
        {
            this.passwordText.Focus();
        }
    }
}
