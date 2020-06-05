main
{
    // 1. сгенерить возможные матчи
    for (i=0; i<CHUNK; i++)
    {
        matches[i] = matchp;                  // сохранить ссылку на первый матч для этой позиции
        matchp = fill_matches(buf,i,matchp);  // сгенерить все матчи этой позиции и записать их в буфер
    }
    // 1.5 Дополнить список matches ссылками на 2/3-байтовые строки
    // 2. выбрать наилучший путь назад
    iterate (CHUNK, price[i]=INT_MAX);  price[0]=0;
    for (i=0; i<CHUNK; i++)
    {
        suggest (i+1, 1, buf[i], price[i] + charPrice(buf[i]));  // предложить на позицию i+1 текущий матч + символ
        lastlen = MINMATCH-1;
        for (our matches)
            while (++lastlen <= len)   // заполним все вакансии от длины пред. матча до длины нынешнего (todo: if len>256, заполнить первые и последние 128 элементов и перещагнуть всё посередине)
            {
                // todo: если дистанция совпадает с одной из 4 предыдущих, то цена будет меньше..
                suggest (i+lastlen, lastlen, dist, price[i] + matchPrice(lastlen,dist)); // цена = цене текущего матча + кодирование нового
            }
    }
    // 3. Записать оптимальный путь от конца к началу
    for (i=CHUNK-1; i; i-=len[i])
    {
        push (len[i], dist[i]);
    }
    // 4. Закодировать найденный путь к Спасению
    while (stack not empty)
    {
        len, dist = pop();
        encode (len,dist);
    }
}

suggest (i, len, dist, match_price)
{
    if (price[i] < match_price)        // новый вариант оказался дешевле
    {
        price[i] = match_price;
        len[i]  = len;
        dist[i] = dist;
    }
}
