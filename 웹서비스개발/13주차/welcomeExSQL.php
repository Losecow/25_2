<!DOCTYPE html>
<html>
<body>

<?php
// 사용자 입력 출력 (PDF 12페이지 위쪽 부분)
echo "Welcome " . $_POST["name"] . "<br>";
echo "Your email address is: " . $_POST["email"] . "<br><br>";
echo "database insertion <br>";

// DB 연결
$mysqli = mysqli_connect("localhost", "root", "", "testDB");

// 연결 실패 시 처리
if (mysqli_connect_errno()) {
    printf("Connect failed: %s\n", mysqli_connect_error());
    exit();
} else {

    // INSERT 문 (PDF 12페이지 예제 그대로)
    $sql = "INSERT INTO exercise_sql (name, email) VALUES ('".$_POST["name"]."','".$_POST["email"]."' )";

    $res = mysqli_query($mysqli, $sql);

    if ($res === TRUE) {
        echo "A record for name and email has been inserted.";
    } else {
        printf("Could not insert record: %s\n", mysqli_error($mysqli));
    }

    mysqli_close($mysqli);
}
?>

</body>
</html>
