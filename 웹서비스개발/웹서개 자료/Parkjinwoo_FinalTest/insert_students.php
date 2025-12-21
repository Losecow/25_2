<?php

$servername = "localhost";
$username = "root";
$password = "";
$dbname = "final";

$conn = new mysqli($servername, $username, $password, $dbname);

if ($conn->connect_error) {
    die("Connection failed: " . $conn->connect_error);
}

$student_id    = $_POST['student_id'];
$student_name  = $_POST['student_name'];
$department_id = $_POST['department_id'];
$birth_year    = $_POST['birth_year'];

$sql = "INSERT INTO students (student_id, student_name, department_id, birth_year)
        VALUES ('$student_id', '$student_name', '$department_id', $birth_year)";

$result = $conn->query($sql);

if ($result) {
    echo "Students 테이블에 성공적으로 삽입되었습니다";
} else {
    echo "Error: " . $conn->error;
}

$conn->close();
?>
