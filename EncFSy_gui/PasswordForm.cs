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
        public string password;

        public PasswordForm()
        {
            InitializeComponent();
            this.AcceptButton = this.okButton;
        }

        private void okButton_Click(object sender, EventArgs e)
        {
            this.password = this.passwordText.Text;
            this.Close();
        }

        private void showPassword_CheckedChanged(object sender, EventArgs e)
        {
            // Toggle password visibility
            this.passwordText.PasswordChar = this.showPassword.Checked ? '\0' : '*';
        }
    }
}
