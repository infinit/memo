$(document).ready(function() {

  var users = $('.estimation table .users td span');
  var usersLabel = $('#usersLabel span');
  var capacities = $('.estimation table .capacity td span');
  var capacityLabel = $('#capacityLabel span');
  var prices = $('.estimation table .price td span');
  var capacityInfinit = $('.estimation table .capacity td.infinit span');

  // Price Update
  function updatePrice() {
    $.each(prices, function(key, element) {
      var service = $(element).parent().attr('class');
      var el = $(users).parent().filter('.' + service);
      var current_users = $(el).find('span').text();
      var price = $(el).find('span').text();
      var new_price;

      new_price = $(element).attr('data-price') * 12 * current_users;

      if (service === 'infinit') {
        new_price = new_price + ($(capacityInfinit).text() * 0.03 * 2);
        console.log($(capacityInfinit).text(), new_price);
      }

      $(element).text(new_price);
    });
  }

  // Users Slider
  $("#usersInput").bind("slider:changed", function (event, data) {
    users.text(data.value);
    usersLabel.text(data.value);

    updatePrice();
  });

  // Capacity Slider
  $("#capacityInput").bind("slider:changed", function (event, data) {
    $.each(capacities, function(key, element) {
      if (!$(element).attr('data-max') || $(element).attr('data-max') >= data.value) {
        $(element).text(data.value);
      } else {
        $(element).text($(element).attr('data-max'));
      }
    });

    updatePrice();
  });

});