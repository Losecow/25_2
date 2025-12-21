<?php

$servername = "localhost";
$username = "root";
$password = "";
$dbname = "final";

$conn = new mysqli($servername, $username, $password, $dbname);

if ($conn->connect_error) {
    die("Connection failed: " . $conn->connect_error);
}

$department_id   = $_POST['department_id'];
$department_name = $_POST['department_name'];
$office_location = $_POST['office_location'];

$sql = "INSERT INTO departments (department_id, department_name, office_location)
        VALUES ('$department_id', '$department_name', '$office_location')";

$result = $conn->query($sql);

if ($result) {
    echo "Departments 테이블에 성공적으로 삽입되었습니다";
} else {
    echo "Error: " . $conn->error;
}

$conn->close();
?>
