Linux'ta `echo $?` komutu, en son çalıştırılan komutun çıkış durumunu (exit status) gösterir.

Çıkış durumu, bir komutun başarılı veya başarısız olduğunu belirten bir sayıdır.
* `0` değeri, komutun başarıyla tamamlandığını gösterir.
* `0` dışında bir değer (genellikle `1` veya daha büyük), bir hata veya başarısızlık olduğunu gösterir.
Örneğin, bir komut çalıştırdıktan sonra `echo $?` kullanarak, o komutun başarılı olup olmadığını kontrol edebilirsiniz.
