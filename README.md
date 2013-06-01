Simple Screenshot Uploader
==========================

This program captures a screenshot, upload, and copy uri to your clipboard.

scrshot.php example:
    <?php
    ini_set('display_errors', '1');
    
    $uploadpath = "/home/dlunch/public_html/scrshot/";
    $uploadfile = $uploadpath.basename($_FILES['file']['name']);
    if(strtolower(substr($_FILES['file']['name'], -3)) != 'png' && strtolower(substr($_FILES['file']['name'], -3)) != 'jpg')
        return;
    
    move_uploaded_file($_FILES['file']['tmp_name'], $uploadfile);
    
    echo "http://dlunch.net/scrshot/".basename($_FILES['file']['name']);
    ?>